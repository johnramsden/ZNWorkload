#include "ze_cache.h"

#include <assert.h>

#include "libzbd/zbd.h"
#include <fcntl.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>

#define SEED 42

// No evict
#define NR_WORKLOADS 4
#define NR_QUERY 20

uint32_t simple_workload[NR_WORKLOADS][NR_QUERY] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}
};

/* Will only print messages (to stdout) when DEBUG is defined */
#ifdef DEBUG
#    define dbg_printf(M, ...) printf("%s: " M, __func__, ##__VA_ARGS__)
#else
#    define dbg_printf(...)
#endif

unsigned char *RANDOM_DATA = NULL;

/**
 * @struct ze_pair
 * @brief Represents a mapping of data to a specific zone and chunk offset.
 *
 * This structure is used to store references to locations within the cache,
 * allowing data to be efficiently retrieved or managed.
 */
struct ze_pair {
    uint32_t zone; /**< Identifier of the zone where the data is stored. */
    uint32_t chunk_offset; /**< Offset within the zone where the data chunk is located. */
};

/**
 * @struct ze_reader
 * @brief Manages concurrent read operations within the cache.
 *
 * The reader structure tracks query execution and workload distribution,
 * ensuring thread-safe access to cached data.
 */
struct ze_reader {
    GMutex lock;          /**< Mutex to synchronize access to the reader state. */
    uint32_t query_index; /**< Index of the next query to be processed. */
    uint32_t workload_index; /**< Index of the workload associated with the reader. */
};

/**
 * @enum ze_zone_state
 * @brief Defines possible states of a cache zone.
 *
 * Zones transition between these states based on their usage and availability.
 */
enum ze_zone_state {
    ZE_ZONE_FREE = 0, /**< The zone is available for new allocations. */
    ZE_ZONE_FULL = 1, /**< The zone is completely occupied and cannot accept new data. */
    ZE_ZONE_ACTIVE = 2, /**< The zone is currently in use and may still have space for new data. */
};

/**
 * @enum ze_eviction_policy
 * @brief Defines eviction policies
 */
enum ze_eviction_policy {
    ZE_EVICT_ZONE = 0, /**< Zone granularity eviction. */
    ZE_EVICT_CHUNK = 1, /**< Chunk granularity eviction. */
};
/**
 * @enum ze_backend
 * @brief Defines SSD backends
 */
enum ze_backend {
    ZE_BACKEND_ZNS = 0, /**< ZNS SSD backend. */
    ZE_BACKEND_BLOCK = 1, /**< Block-interface backend. */
};

/**
 * @struct ze_cache
 * @brief Represents a cache system that manages data storage in predefined zones.
 *
 * This structure is responsible for managing zones, including tracking active,
 * free, and recently used zones. It supports parallel insertion, LRU eviction,
 * and mapping of data IDs to specific zones and offsets.
 */
struct ze_cache {
    enum ze_backend backend;        /**< SSD backend. */
    int fd;                         /**< File descriptor for associated disk. */
    uint32_t max_nr_active_zones;   /**< Maximum number of zones that can be active at once. */
    uint32_t nr_active_zones;       /**< Current number of active zones. */
    uint32_t nr_zones;              /**< Total number of zones availible. */
    uint64_t max_zone_chunks;       /**< Maximum number of chunks a zone can hold. */
    size_t chunk_sz;                /**< Size of each chunk in bytes. */
    uint64_t zone_cap;              /**< Maximum storage capacity per zone in bytes. */

    // Cache structures
    GQueue *lru_queue;              /**< Least Recently Used (LRU) queue for zone eviction. */
    GMutex cache_lock;              /**< Mutex to protect cache operations. */
    GHashTable *zone_map;           /**< Hash table mapping data IDs to `ze_pair` entries in the LRU queue. */

    // Free/active zone management
    GQueue *free_list;              /**< Queue of zones that are currently free and available for allocation. */
    GQueue *active_queue;           /**< Queue of zones that are currently active and in use. */
    enum ze_zone_state *zone_state; /**< Array representing the state of each zone. */
    enum ze_eviction_policy eviction_policy; /**< Eviction policy. */

    struct ze_reader reader;        /**< Reader structure for tracking workload location. */
};

/**
 * @struct ze_thread_data
 * @brief Holds thread-specific data for interacting with the cache.
 *
 * This structure associates a thread with a specific cache instance
 * and provides a unique thread identifier.
 */
struct ze_thread_data {
    struct ze_cache *cache; /**< Pointer to the cache instance associated with this thread. */
    uint32_t tid; /**< Unique identifier for the thread. */
};

/**
 * @brief Generates a buffer filled with random bytes.
 *
 * This function allocates a buffer of the specified size and fills it with
 * random values ranging from 0 to 255. The random number generator is seeded
 * with a predefined value (`SEED`), which may result in deterministic output
 * unless `SEED` is properly randomized elsewhere.
 *
 * @param size The number of bytes to allocate and populate with random values.
 * @return A pointer to the allocated buffer containing random bytes, or NULL
 *         if allocation fails or size is zero.
 *
 * @note The caller is responsible for freeing the allocated buffer.
 */
static unsigned char *
generate_random_buffer(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // Allocate memory for the buffer
    unsigned char *buffer = (unsigned char *)malloc(size);
    if (buffer == NULL) {
        return NULL;
    }

    srand(SEED);

    for (size_t i = 0; i < size; i++) {
        buffer[i] = (unsigned char)(rand() % 256); // Random byte (0-255)
    }

    return buffer;
}

/**
 * @brief Exit if NOMEM
 */
void
nomem() {
    fprintf(stderr, "ERROR: No memory\n");
    exit(ENOMEM);
}

/**
 * @brief Uniform rand with limit
 *
 * return a random number between 0 and limit inclusive.
 *
 * Cite: https://stackoverflow.com/a/2999130
 */
int
rand_lim(int limit) {
    int divisor = RAND_MAX/(limit+1);
    int retval;

    do {
        retval = rand() / divisor;
    } while (retval > limit);

    return retval;
}

/**
 * @brief Prints information about a Zoned Block Device (ZBD).
 *
 * This function outputs detailed information about a given `zbd_info` structure,
 * including vendor details, sector counts, zone properties, and model type.
 *
 * @param info Pointer to a `struct zbd_info` containing ZBD details.
 *
 * @cite https://github.com/westerndigitalcorporation/libzbd/blob/master/include/libzbd/zbd.h
 */
[[maybe_unused]] static void
print_zbd_info(struct zbd_info *info) {
    printf("vendor_id=%s\n", info->vendor_id);
    printf("nr_sectors=%llu\n", info->nr_sectors);
    printf("nr_lblocks=%llu\n", info->nr_lblocks);
    printf("nr_pblocks=%llu\n", info->nr_pblocks);
    printf("zone_size (bytes)=%llu\n", info->zone_size);
    printf("zone_sectors=%u\n", info->zone_sectors);
    printf("lblock_size=%u\n", info->lblock_size);
    printf("pblock_size=%u\n", info->pblock_size);
    printf("nr_zones=%u\n", info->nr_zones);
    printf("max_nr_open_zones=%u\n", info->max_nr_open_zones);
    printf("max_nr_active_zones=%u\n", info->max_nr_active_zones);
    printf("model=%u\n", info->model);
}

/**
 * @brief Get zone capacity
 *
 * @param[in] fd open zone file descriptor
 * @param[out] zone_cap zone capacity
 * @return non-zero on error
 */
static int
zone_cap(int fd, uint64_t *zone_capacity) {
    off_t ofst = 0;
    off_t len = 1;
    struct zbd_zone zone;
    unsigned int nr_zones;
    int ret = zbd_report_zones(fd, ofst, len, ZBD_RO_ALL, &zone, &nr_zones);
    if (ret != 0) {
        return ret;
    }
    *zone_capacity = zone.capacity;
    return ret;
}

#ifdef VERIFY
/**
 * @brief Verifies the integrity of a `ze_cache` structure using assertions.
 *
 * This function checks whether essential components of the `ze_cache` structure
 * are properly initialized. If any assertion fails, the program will terminate,
 * ensuring that the cache is in a valid state before proceeding.
 *
 * @param zam Pointer to the `ze_cache` structure to validate.
 *
 * @note This function is only enabled when `VERIFY` is defined. If `VERIFY` is not
 *       defined, `VERIFY_ZE_CACHE(ptr)` does nothing.
 */
static void
check_assertions_ze_cache(struct ze_cache * zam) {
    // Asserts should always pass
    assert(zam != NULL);
    assert(zam->zone_map != NULL);
    assert(zam->zone_state != NULL);
    assert(zam->active_queue != NULL);
    assert(zam->lru_queue != NULL);
    assert(zam->free_list != NULL);
    assert(zam->fd > 0);
}

/**
 * @brief Macro to invoke `check_assertions_ze_cache` when `VERIFY` is defined.
 *
 * This macro calls `check_assertions_ze_cache(ptr)`, ensuring that the provided
 * `ze_cache` instance is in a valid state. When `VERIFY` is not defined, this
 * macro does nothing.
 *
 * @param ptr Pointer to the `ze_cache` structure to verify.
 */
#define VERIFY_ZE_CACHE(ptr) check_assertions_ze_cache(ptr)
#else
#define VERIFY_ZE_CACHE(ptr) // Do nothing
#endif

static void
ze_print_cache(struct ze_cache * cache) {
    (void)cache;
#ifdef DEBUG
        printf("\tchunk_sz=%lu\n", cache->chunk_sz);
        printf("\tnr_zones=%u\n", cache->nr_zones);
        printf("\tzone_cap=%" PRIu64 "\n", cache->zone_cap);
        printf("\tmax_zone_chunks=%" PRIu64 "\n", cache->max_zone_chunks);
#endif
}

/**
 * @brief Initializes the free list for the cache zones.
 *
 * This function allocates a new queue to manage free zones and populates it
 * with all available zone indices. If memory allocation fails, it invokes
 * `nomem()` to handle the error.
 *
 * @param cache Pointer to the `ze_cache` structure whose free list is being initialized.
 *
 * @note The function assumes that `cache->nr_zones` is set before calling it.
 *       Each zone index is stored as a pointer using `GINT_TO_POINTER(i)`.
 */
static void
ze_init_free_list(struct ze_cache * cache) {
    cache->free_list = g_queue_new();
    if (cache->free_list == NULL) {
        nomem();
    }
    for (uint32_t i = 0; i < cache->nr_zones; i++) {
        g_queue_push_tail(cache->free_list, GINT_TO_POINTER(i));
    }
}

/**
 * @brief Initializes a `ze_cache` structure with the given parameters.
 *
 * This function sets up the cache by initializing its fields, creating the required
 * data structures (hash table, queues, state array), and setting up synchronization
 * mechanisms. It also verifies the integrity of the initialized cache.
 *
 * @param cache Pointer to the `ze_cache` structure to initialize.
 * @param info Pointer to `zbd_info` providing zone details.
 * @param chunk_sz The size of each chunk in bytes.
 * @param zone_cap The maximum capacity per zone in bytes.
 * @param fd File descriptor associated with the disk
 * @param eviction_policy Eviction policy used
 */
static void
ze_init_cache(struct ze_cache * cache, struct zbd_info *info, size_t chunk_sz, uint64_t zone_cap,
              int fd, enum ze_eviction_policy eviction_policy, enum ze_backend backend) {
    cache->fd = fd;
    cache->chunk_sz = chunk_sz;
    cache->nr_zones = info->nr_zones;
    cache->zone_cap = zone_cap;
    cache->max_nr_active_zones = info->max_nr_active_zones;
    cache->nr_active_zones = 0;
    cache->zone_cap = zone_cap;
    cache->max_zone_chunks = zone_cap / chunk_sz;
    cache->eviction_policy = eviction_policy;
    cache->backend = backend;

#ifdef DEBUG
        printf("Initialized cache:\n");
        printf("\tchunk_sz=%lu\n", cache->chunk_sz);
        printf("\tnr_zones=%u\n", cache->nr_zones);
        printf("\tzone_cap=%" PRIu64 "\n", cache->zone_cap);
        printf("\tmax_zone_chunks=%" PRIu64 "\n", cache->max_zone_chunks);
        printf("\tmax_nr_active_zones=%" PRIu64 "\n", cache->max_nr_active_zones);
#endif

    // Create a hash table with integer keys and values
    cache->zone_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    if (cache->zone_map == NULL) {
        nomem();
    }

    // Init lists:

    cache->zone_state = g_new0(enum ze_zone_state, cache->nr_zones);
    if (cache->zone_state == NULL) {
         nomem();
    }

    cache->lru_queue = g_queue_new();
    if (cache->lru_queue == NULL) {
         nomem();
    }

    cache->active_queue = g_queue_new();
    if (cache->active_queue == NULL) {
         nomem();
    }
    
    ze_init_free_list(cache);

    g_mutex_init(&cache->cache_lock);
    g_mutex_init(&cache->reader.lock);
    cache->reader.query_index = 0;
    cache->reader.workload_index = 0;

    VERIFY_ZE_CACHE(cache);
}

/**
 * @brief Destroys and cleans up a `ze_cache` structure.
 *
 * This function frees all dynamically allocated resources associated with the cache,
 * including hash tables, queues, and state arrays. It also closes the file descriptor
 * and clears associated mutexes to ensure proper cleanup.
 *
 * @param zam Pointer to the `ze_cache` structure to be destroyed.
 *
 * @note After calling this function, the `ze_cache` structure should not be used
 *       unless it is reinitialized.
 * @note The function assumes that `zam` is properly initialized before being passed.
 */
static void
ze_destroy_cache(struct ze_cache * zam) {
    g_hash_table_destroy(zam->zone_map);
    g_free(zam->zone_state);
    g_queue_free_full(zam->active_queue, g_free);
    g_queue_free_full(zam->lru_queue, g_free);

    zbd_close(zam->fd);

    g_mutex_clear(&zam->cache_lock);
    g_mutex_clear(&zam->reader.lock);
}

static void
ze_cache_get(struct ze_cache * zam, const uint32_t id) {
    VERIFY_ZE_CACHE(zam);

    g_mutex_lock(&zam->cache_lock);

    if (g_hash_table_contains(zam->zone_map, GINT_TO_POINTER(id))) {
        printf("Cache ID %u in cache\n", id);
    } else {
        printf("Cache ID %u not in cache\n", id);
        struct ze_pair *zp = g_new(struct ze_pair, 1);
        if (zp == NULL) {
            nomem();
        }
        zp->zone = 1;
        zp->chunk_offset = 2;
        g_hash_table_insert(zam->zone_map, GINT_TO_POINTER(id), zp);
    }

    g_mutex_unlock(&zam->cache_lock);
}

// Task function
void task_function(gpointer data, gpointer user_data) {
    (void)user_data;
    struct ze_thread_data * thread_data = data;

    printf("Task %d started by thread %p\n", thread_data->tid, g_thread_self());
    // ze_print_cache(thread_data->cache);
    while (true) {
        g_mutex_lock(&thread_data->cache->reader.lock);

        // When we exceed Query number, go next workload
        if (thread_data->cache->reader.query_index >= NR_QUERY) {
            thread_data->cache->reader.query_index = 0;
            thread_data->cache->reader.workload_index++;
        }
        if (thread_data->cache->reader.workload_index >= NR_WORKLOADS) {
            g_mutex_unlock(&thread_data->cache->reader.lock);
            break;
        }

        uint32_t qi = thread_data->cache->reader.query_index++;
        uint32_t wi = thread_data->cache->reader.workload_index;
        g_mutex_unlock(&thread_data->cache->reader.lock);

        // Assertions to validate state
        assert(qi < NR_QUERY);
        assert(wi < NR_WORKLOADS);

        ze_cache_get(thread_data->cache, simple_workload[wi][qi]);
        printf("[%d]: ze_cache_get(simple_workload[%d][%d]=%d)\n", thread_data->tid, wi, qi, simple_workload[wi][qi]);
    }
    printf("Task %d finished by thread %p\n", thread_data->tid, g_thread_self());
}

int
main(int argc, char **argv) {
    zbd_set_log_level(ZBD_LOG_ERROR);

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <DEVICE> <CHUNK_SZ> <THREADS>\n", argv[0]);
        return -1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Please run as root\n");
        return -1;
    }

    char *device = argv[1];

    size_t chunk_sz = strtoul(argv[2], NULL, 10);
    int32_t nr_threads = strtol(argv[3], NULL, 10);
    int32_t nr_eviction_threads = 1;

    printf("Running with configuration:\n"
            "\tDevice name: %s\n"
            "\tChunk size: %lu\n"
            "\tWorker threads: %u\n"
            "\tEviction threads: %u\n",
            device, chunk_sz, nr_threads, nr_eviction_threads);

#ifdef DEBUG
    printf("\tDEBUG=on\n");
#endif
#ifdef VERIFY
    printf("\tVERIFY=on\n");
#endif

    struct zbd_info info;
    int fd = zbd_open(device, O_RDWR, &info);
    if (fd < 0) {
        fprintf(stderr, "Error opening device: %s\n", device);
        return fd;
    }

    uint64_t zone_capacity;
    int ret = zone_cap(fd, &zone_capacity);
    if (ret != 0) {
        fprintf(stderr, "Couldn't report zone info\n");
        return ret;
    }

    struct ze_cache zam = {0};
    ze_init_cache(&zam, &info, chunk_sz, zone_capacity, fd, ZE_EVICT_ZONE, ZE_BACKEND_ZNS);

    RANDOM_DATA = generate_random_buffer(chunk_sz);
    if (RANDOM_DATA == NULL) {
         nomem();
    }

    GError *error = NULL;
    // Create a thread pool with a maximum of nr_threads
    GThreadPool *pool = g_thread_pool_new(task_function, NULL, nr_threads, FALSE, &error);
    if (error) {
        fprintf(stderr, "Error creating thread pool: %s\n", error->message);
        return 1;
    }

    // Push tasks to the thread pool
    struct ze_thread_data *thread_data = g_new(struct ze_thread_data, nr_threads);
    for (int i = 0; i < nr_threads; i++) {
        thread_data[i].tid = i;
        thread_data[i].cache = &zam;
        g_thread_pool_push(pool, &thread_data[i], &error);
        if (error) {
            fprintf(stderr, "Error pushing task: %s\n", error->message);
            return 1;
        }
    }

    // Wait for tasks to finish and free the thread pool
    g_thread_pool_free(pool, FALSE, TRUE);

    // Cleanup
    ze_destroy_cache(&zam);
    g_free(thread_data);
    free(RANDOM_DATA);
    return 0;
}