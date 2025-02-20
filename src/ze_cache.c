// For pread
#define _XOPEN_SOURCE 500

#include "ze_cache.h"

#include "libzbd/zbd.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <linux/fs.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define SEED 42

#define EVICT_HIGH_THRESH 2
#define EVICT_LOW_THRESH 10

#define MICROSECS_PER_SECOND 1000000
#define EVICT_SLEEP_US ((long)(0.5 * MICROSECS_PER_SECOND))

#define MAX_OPEN_ZONES 14
#define WRITE_GRANULARITY 4096

#define BLOCK_ZONE_CAPACITY ((long)1077 * 1024 * 1024)

// No evict
#define NR_WORKLOADS 4
#define NR_QUERY 20

uint32_t simple_workload[NR_WORKLOADS][NR_QUERY] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}};

unsigned char *RANDOM_DATA = NULL;

/* Will only print messages (to stdout) when DEBUG is defined */
#ifdef DEBUG
#    define dbg_printf(M, ...) printf("%s: " M, __func__, ##__VA_ARGS__)
#else
#    define dbg_printf(...)
#endif

// Get write pointer from (zone, chunk)
#define CHUNK_POINTER(z_sz, c_sz, c_num, z_num)                                                    \
    (((uint64_t) (z_sz) * (uint64_t) (z_num)) + ((uint64_t) (c_num) * (uint64_t) (c_sz)))

/**
 * @struct ze_pair
 * @brief Represents a mapping of data to a specific zone and chunk offset.
 *
 * This structure is used to store references to locations within the cache,
 * allowing data to be efficiently retrieved or managed.
 */
struct ze_pair {
    uint32_t zone;         /**< Identifier of the zone where the data is stored. */
    uint32_t chunk_offset; /**< Offset within the zone where the data chunk is located. */
    uint32_t id;           /**< Unique ID */
    bool in_use;           /**< Defines if ze_pair is in use. */
};

/**
 * @struct ze_reader
 * @brief Manages concurrent read operations within the cache.
 *
 * The reader structure tracks query execution and workload distribution,
 * ensuring thread-safe access to cached data.
 */
struct ze_reader {
    GMutex lock;             /**< Mutex to synchronize access to the reader state. */
    uint32_t query_index;    /**< Index of the next query to be processed. */
    uint32_t workload_index; /**< Index of the workload associated with the reader. */
};

/**
 * @enum ze_zone_condition
 * @brief Defines possible conditions of a cache zone.
 *
 * Zones transition between these states based on their usage and availability.
 */
enum ze_zone_condition {
    ZE_ZONE_FREE = 0,   /**< The zone is available for new allocations. */
    ZE_ZONE_FULL = 1,   /**< The zone is completely occupied and cannot accept new data. */
    ZE_ZONE_ACTIVE = 2, /**< The zone is currently in use and may still have space for new data. */
};

/**
 * @enum ze_zone_state
 * @brief Defines state of a zone
 */
struct ze_zone_state {
    uint32_t chunk_loc;                /**< Current location for next chunk insert. */
    enum ze_zone_condition zone_state; /**< Possible conditions zone. */
};

/**
 * @enum ze_eviction_policy
 * @brief Defines eviction policies
 */
enum ze_eviction_policy {
    ZE_EVICT_ZONE = 0,  /**< Zone granularity eviction. */
    ZE_EVICT_PROMOTE_ZONE = 1,  /**< Zone granularity eviction with promotion. */
    ZE_EVICT_CHUNK = 2, /**< Chunk granularity eviction. */
};
/**
 * @enum ze_backend
 * @brief Defines SSD backends
 */
enum ze_backend {
    ZE_BACKEND_ZNS = 0,   /**< ZNS SSD backend. */
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
    enum ze_backend backend;      /**< SSD backend. */
    int fd;                       /**< File descriptor for associated disk. */
    uint32_t max_nr_active_zones; /**< Maximum number of zones that can be active at once. */
    uint32_t nr_active_zones;     /**< Current number of active zones. */
    uint32_t nr_zones;            /**< Total number of zones availible. */
    uint64_t max_zone_chunks;     /**< Maximum number of chunks a zone can hold. */
    size_t chunk_sz;              /**< Size of each chunk in bytes. */
    uint64_t zone_cap;            /**< Maximum storage capacity per zone in bytes. */

    // Cache structures
    GQueue *lru_queue;    /**< Least Recently Used (LRU) queue for zone eviction. */
    GMutex cache_lock;    /**< Mutex to protect cache operations. */
    GHashTable *zone_map; /**< Hash table mapping data IDs to `ze_pair` entries in the LRU queue. */

    // Free/active zone management
    GQueue *free_list; /**< Queue of zones that are currently free and available for allocation. */
    // Zones that have some space and are still active
    GQueue *active_queue; /**< Queue of zones that are currently active and in use. */

    struct ze_pair **zone_pool; /**< Pool of zone pairs */

    enum ze_eviction_policy eviction_policy; /**< Eviction policy. */
    struct ze_zone_state *zone_state;        /**< Array representing the state of each Zone */

    struct ze_reader reader; /**< Reader structure for tracking workload location. */
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
    uint32_t tid;           /**< Unique identifier for the thread. */
    bool done;              /**< Marks completed */
};

/**
 * @brief Prints all key-value pairs in a GHashTable.
 *
 * This function assumes that the GHashTable uses integer keys and values
 * stored as GINT_TO_POINTER. The keys are pointers to integers, and
 * the values are stored as integer pointers.
 *
 * @param hash_table A pointer to the GHashTable to print.
 */
[[maybe_unused]] static void
print_g_hash_table(char *name, GHashTable *hash_table) {
    GHashTableIter iter;
    gpointer key, value;

    printf("hash table %s:\n", name);

    g_hash_table_iter_init(&iter, hash_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        struct ze_pair *zp = (struct ze_pair *) value;
        printf("\tKey: %d, Value: zone=%u, chunk=%u, id=%u, in_use=%s\n", GPOINTER_TO_INT(key), zp->zone,
               zp->chunk_offset, zp->id, zp->in_use ? "true" : "false");
    }
}

#ifdef DEBUG
#    define dbg_print_g_hash_table(name, hash_table) print_g_hash_table(name, hash_table)
#else
#    define dbg_print_g_hash_table(...)
#endif

/**
 * @brief Prints all elements in a GQueue.
 *
 * This function assumes that the GQueue stores integers using GINT_TO_POINTER.
 *
 * @param queue A pointer to the GQueue to print.
 */
static void
print_g_queue(char *name, GQueue *queue) {
    printf("Printing queue %s: ", name);
    for (GList *node = queue->head; node != NULL; node = node->next) {
        printf("%d ", GPOINTER_TO_INT(node->data));
    }
    puts("");
}

#ifdef DEBUG
#    define dbg_print_g_queue(name, queue) print_g_queue(name, queue)
#else
#    define dbg_print_g_queue(...)
#endif

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
    unsigned char *buffer = (unsigned char *) malloc(size);
    if (buffer == NULL) {
        return NULL;
    }

    srand(SEED);

    for (size_t i = 0; i < size; i++) {
        buffer[i] = (unsigned char) (rand() % 256); // Random byte (0-255)
    }

    return buffer;
}

/**
 * @brief Exit if NOMEM
 */
static void
nomem() {
    fprintf(stderr, "ERROR: No memory\n");
    exit(ENOMEM);
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
 * @param cache Pointer to the `ze_cache` structure to validate.
 *
 * @note This function is only enabled when `VERIFY` is defined. If `VERIFY` is not
 *       defined, `VERIFY_ZE_CACHE(ptr)` does nothing.
 */
static void
check_assertions_ze_cache(struct ze_cache *cache) {
    assert(cache != NULL);
    assert(cache->zone_map != NULL);
    assert(cache->zone_state != NULL);
    assert(cache->active_queue != NULL);
    assert(cache->lru_queue != NULL);
    assert(cache->free_list != NULL);
    assert(cache->fd > 0);

    assert(cache->zone_pool != NULL);
    for (uint32_t i = 0; i < cache->nr_zones; i++) {
        assert(cache->zone_pool[i] != NULL);
    }

    uint32_t zone_state_active = 0, zone_state_free = 0, zone_state_full = 0, zone_state_total = 0;

    for (GList *node = cache->active_queue->head; node != NULL; node = node->next) {
        int data = GPOINTER_TO_INT(node->data);
        assert(cache->zone_state[data].zone_state == ZE_ZONE_ACTIVE);
        zone_state_active++;
    }
    for (GList *node = cache->free_list->head; node != NULL; node = node->next) {
        int data = GPOINTER_TO_INT(node->data);
        assert(cache->zone_state[data].zone_state == ZE_ZONE_FREE);
        zone_state_free++;
    }
    for (uint32_t i = 0; i < cache->nr_zones; i++) {
        if (cache->zone_state[i].zone_state == ZE_ZONE_FULL) {
            zone_state_full++;
        }
    }

    if (cache->nr_active_zones != zone_state_active) {
        dbg_printf("nr_active_zones = %d != zone_state_active = %d\n", cache->nr_active_zones,
                   zone_state_active);
    }
    assert(cache->nr_active_zones == zone_state_active);

    zone_state_total = zone_state_active + zone_state_free + zone_state_full;
    if (cache->nr_zones != zone_state_total) {
        dbg_printf("cache->nr_zones=%u != zone_state_total=%u, active=%u, full=%u, free=%u\n",
                   cache->nr_zones, zone_state_total, zone_state_active, zone_state_full,
                   zone_state_total);
    }
    assert(cache->nr_zones == zone_state_total);
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
#    define VERIFY_ZE_CACHE(ptr) check_assertions_ze_cache(ptr)
#else
#    define VERIFY_ZE_CACHE(ptr) // Do nothing
#endif

[[maybe_unused]] static void
ze_print_cache(struct ze_cache *cache) {
    (void) cache;
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
ze_init_free_list(struct ze_cache *cache) {
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
ze_init_cache(struct ze_cache *cache, struct zbd_info *info, size_t chunk_sz, uint64_t zone_cap,
              int fd, enum ze_eviction_policy eviction_policy, enum ze_backend backend) {
    cache->fd = fd;
    cache->chunk_sz = chunk_sz;
    cache->nr_zones = info->nr_zones;
    cache->zone_cap = zone_cap;
    cache->max_nr_active_zones =
        info->max_nr_active_zones == 0 ? MAX_OPEN_ZONES : info->max_nr_active_zones;
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
    printf("\tmax_nr_active_zones=%u\n", cache->max_nr_active_zones);
#endif

    // Create a hash table with integer keys and values
    cache->zone_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (cache->zone_map == NULL) {
        nomem();
    }

    // Init lists
    cache->zone_state = g_new0(struct ze_zone_state, cache->nr_zones);
    if (cache->zone_state == NULL) {
        nomem();
    }

    cache->zone_pool = g_new0(struct ze_pair *, cache->nr_zones);
    if (cache->zone_pool == NULL) {
        nomem();
    }
    for (uint32_t i = 0; i < cache->nr_zones; i++) {
        cache->zone_pool[i] = g_new0(struct ze_pair, cache->max_zone_chunks);
        if (cache->zone_pool[i] == NULL) {
            nomem();
        }
        for (uint32_t j = 0; j < cache->max_zone_chunks; j++) {
            cache->zone_pool[i][j].in_use = false;
        }
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
 * @param cache Pointer to the `ze_cache` structure to be destroyed.
 *
 * @note After calling this function, the `ze_cache` structure should not be used
 *       unless it is reinitialized.
 * @note The function assumes that `zam` is properly initialized before being passed.
 */
static void
ze_destroy_cache(struct ze_cache *cache) {
    g_hash_table_destroy(cache->zone_map);
    g_free(cache->zone_state);
    g_queue_free_full(cache->active_queue, g_free);
    g_queue_free_full(cache->lru_queue, g_free);

    if(cache->backend == ZE_BACKEND_ZNS) {
        zbd_close(cache->fd);
    } else {
        close(cache->fd);
    }

    g_mutex_clear(&cache->cache_lock);
    g_mutex_clear(&cache->reader.lock);
}

/**
 * @brief Read a chunk from disk
 *
 * @param cache Pointer to the `ze_cache` structure
 * @param zone_pair Chunk, zone pair
 * @return Buffer read from disk, to be freed by caller
 */
static unsigned char *
ze_read_from_disk(struct ze_cache *cache, struct ze_pair *zone_pair) {
    unsigned char *data = malloc(cache->chunk_sz);
    if (data == NULL) {
        nomem();
    }

    unsigned long long wp =
        CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, zone_pair->chunk_offset, zone_pair->zone);

    dbg_printf("[%u,%u] read from write pointer: %llu\n", zone_pair->zone, zone_pair->chunk_offset,
               wp);

    size_t b = pread(cache->fd, data, cache->chunk_sz, wp);
    if (b != cache->chunk_sz) {
        fprintf(stderr, "Couldn't read from fd\n");
        free(data);
        return NULL;
    }

    return data;
}

/**
 * @brief Open a zone
 *
 * @param cache cache Pointer to the `ze_cache` structure, caller is responsible for locking
 * @param zone_id Zone to open
 *
 * @return Returns 0 on success and -1 otherwise.
 */
static int
ze_open_zone(struct ze_cache *cache, uint32_t zone_id) {
    if (cache->zone_state[zone_id].zone_state == ZE_ZONE_ACTIVE) {
        dbg_printf("Zone already open\n");
        return 0;
    }
    if (cache->nr_active_zones >= cache->max_nr_active_zones) {
        dbg_printf("Already at active zone limit\n");
        return -1;
    }

    unsigned long long wp = CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, 0, zone_id);
    dbg_printf("Opening zone %u, zone pointer %llu\n", zone_id, wp);
    
    int ret;
    if (cache->backend == ZE_BACKEND_ZNS) {
        ret = zbd_open_zones(cache->fd, wp, 1);
        if (ret != 0) {
            return ret;
        }   
    } else {
        // We don't have to do anything with block devices
        ret = 0;
    }
    
    cache->nr_active_zones++;
    cache->zone_state[zone_id].zone_state = ZE_ZONE_ACTIVE;
    cache->zone_state[zone_id].chunk_loc = 0;
    return ret;
}

/**
 * @brief Close a zone
 *
 * @param cache cache Pointer to the `ze_cache` structure, caller is responsible for locking
 * @param zone_id Zone to close
 *
 * @return Returns 0 on success and -1 otherwise.
 */
static int
ze_close_zone(struct ze_cache *cache, uint32_t zone_id) {
    if (cache->zone_state[zone_id].zone_state == ZE_ZONE_FULL) {
        dbg_printf("Zone already closed\n");
        return 0;
    }

    unsigned long long wp = CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, 0, zone_id);
    dbg_printf("Closing zone %u, zone pointer %llu\n", zone_id, wp);
    zbd_set_log_level(ZBD_LOG_ERROR);

    // FOR DEBUGGING ZONE STATE
    // struct zbd_zone zone;
    // unsigned int nr_zones;
    // if (zbd_report_zones(cache->fd, wp, 1, ZBD_RO_ALL, &zone, &nr_zones) == 0) {
    //     printf("Zone state before close: %u\n", zone.cond == ZBD_ZONE_COND_FULL);
    // }

    // NOTE: FULL ZONES ARE NOT ACTIVE
    int ret = zbd_finish_zones(cache->fd, wp, cache->zone_cap);
    if (ret != 0) {
        dbg_printf("Failed to close zone %u\n", zone_id);
        return ret;
    }
    // EXPLICIT CLOSE FAILS ON NULLBLK, TODO: TEST ON REAL DEV ON CORTES
    // ret = zbd_close_zones(cache->fd, wp, cache->zone_cap);
    // if (ret != 0) {
    //     return ret;
    // }
    cache->nr_active_zones--;
    cache->zone_state[zone_id].zone_state = ZE_ZONE_FULL;
    cache->zone_state[zone_id].chunk_loc = 0;

    return ret;
}

/**
 * @brief Get an active zone. If there are no active zones, create a new one from a free zone.
 *
 * @param cache Pointer to the `ze_cache` structure, caller is responsible for locking
 * @param[out] The requested zone Id if successful
 * @return Status code. 0 for success and -1 for failures.
 *
 * TODO: Foreground GC if needed (When we are completely full)
 */
static int
ze_get_active_zone(struct ze_cache *cache, uint32_t *zone_id) {

    int active_q_empty = g_queue_is_empty(cache->active_queue);
    int free_q_empty = g_queue_is_empty(cache->free_list);

    // Early exit / GC
    if (active_q_empty && free_q_empty && cache->nr_active_zones < cache->max_nr_active_zones) {
        dbg_printf("shouldnt really occur nr_active_zones=%u, max_nr_active_zones=%u, "
                   "active_q_empty=%d, free_q_empty=%d\n",
                   cache->nr_active_zones, cache->max_nr_active_zones, active_q_empty,
                   free_q_empty);
        // TODO: GC, nothing avail, shouldnt really occur
        return -1;
    }

    // Use an existing active zone
    if (!active_q_empty) {
        dbg_print_g_queue("active,queue", cache->active_queue);
        *zone_id = GPOINTER_TO_INT(g_queue_pop_head(cache->active_queue));
        return 0;
    }

    // No active zones, get a new one
    if (cache->nr_active_zones < cache->max_nr_active_zones) {
        *zone_id = GPOINTER_TO_INT(g_queue_pop_head(cache->free_list));
        int ret = ze_open_zone(cache, *zone_id);
        if (ret != 0) {
            dbg_printf("Failed to open zone\n");
            g_queue_push_head(cache->free_list, GINT_TO_POINTER(*zone_id));
            return ret;
        }
    }

    return 0;
}

/**
 * @brief Write buffer to disk
 *
 * @param to_write Total size of write
 * @param buffer   Buffer to write to disk
 * @param fd       Disk file descriptor
 * @param write_size Granularity for each write
 * @return int     Non-zero on error
 *
 * @note Be careful write size is not too large otherwise you can get errors
 */
static int
ze_write_out(int fd, size_t to_write, const unsigned char *buffer, ssize_t write_size,
             unsigned long long wp_start) {
    ssize_t bytes_written;
    size_t total_written = 0;

    errno = 0;
    while (total_written < to_write) {
        bytes_written = pwrite(fd, buffer + total_written, write_size, wp_start + total_written);
        fsync(fd);
        // dbg_printf("Wrote %ld bytes to fd at offset=%llu\n", bytes_written,
        // wp_start+total_written);
        if ((bytes_written == -1) || (errno != 0)) {
            dbg_printf("Error: %s\n", strerror(errno));
            dbg_printf("Couldn't write to fd\n");
            return -1;
        }
        total_written += bytes_written;
        // dbg_printf("total_written=%ld bytes of %zu\n", total_written, to_write);
    }
    return 0;
}

/**
 * Allocate a buffer prefixed by `zone_id`, with the rest being `RANDOM_DATA`
 *
 * @param cache Pointer to the `ze_cache` structure.
 * @param zone_id ID to write to first 4 bytes
 * @return Allocated buffer or NULL, caller is responsible for free
 */
static unsigned char *
ze_gen_write_buffer(struct ze_cache *cache, uint32_t zone_id) {
    unsigned char *data = malloc(cache->chunk_sz);
    if (data == NULL) {
        nomem();
    }

    memcpy(data, RANDOM_DATA, cache->chunk_sz);
    // Metadata
    memcpy(data, &zone_id, sizeof(uint32_t));

    return data;
}

/**
 * Get the next available `ze_pair`
 *
 * @param cache Pointer to the `ze_cache` structure.
 * @param zone_id Zone id to allocate `ze_pair` from
 * @param id UID to map to `ze_pair`
 * @return ze_pair (backed by zone_pool, do not free as caller)
 */
static struct ze_pair *
ze_get_next_ze_pair(const struct ze_cache * cache, const uint32_t zone_id, const uint32_t id) {
    struct ze_pair * zp;
    uint32_t chunk_offset = cache->zone_state[zone_id].chunk_loc;
    zp = &cache->zone_pool[zone_id][chunk_offset];
    zp->zone = zone_id;
    zp->chunk_offset = chunk_offset;
    zp->in_use = true;
    zp->id = id;
    return zp;
}

/**
 * @brief Get data from cache
 *
 * Gets data from cache if present, otherwise pulls from emulated remote
 *
 * @param cache Pointer to the `ze_cache` structure.
 * @param id Cache item ID to get
 * @returns Buffer of data recieved or NULL on error (callee is responsible for freeing)
 */
static unsigned char *
ze_cache_get(struct ze_cache *cache, const uint32_t id) {
    unsigned char *data = NULL;
    gpointer id_ptr = GINT_TO_POINTER(id);

    g_mutex_lock(&cache->cache_lock);

    VERIFY_ZE_CACHE(cache);

    // In cache. Read the data
    if (g_hash_table_contains(cache->zone_map, id_ptr)) {
        struct ze_pair *zp;
        zp = g_hash_table_lookup(cache->zone_map, id_ptr);
        dbg_printf("Cache ID %u in cache at zone_pointer [%u,%u]\n", id, zp->zone,
                   zp->chunk_offset);
        data = ze_read_from_disk(cache, zp);
    } else {

        // Not in cache. Emulates pulling in data from a remote source by filling in a cache entry with random bytes
        dbg_printf("Cache ID %u not in cache\n", id);
        uint32_t zone_id;
        // Get an active zone and its free chunk
        int ret = ze_get_active_zone(cache, &zone_id);
        if (ret != 0) {
            dbg_printf("Failed to get active zone\n");
            g_mutex_unlock(&cache->cache_lock);
            return NULL;
        }

        struct ze_pair *zp = ze_get_next_ze_pair(cache, zone_id, id);

        data = ze_gen_write_buffer(cache, id);

        // Write buffer to disk, 4kb blocks at a time
        unsigned long long wp =
            CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, zp->chunk_offset, zp->zone);
        if (ze_write_out(cache->fd, cache->chunk_sz, data, WRITE_GRANULARITY, wp) != 0) {
            dbg_printf("Couldn't write to fd at wp=%llu\n", wp);
            // TODO: Is zone out of sync now? What to do with activee?
            g_mutex_unlock(&cache->cache_lock);
            return NULL;
        }

        // Associate the data id with the location on disk
        g_hash_table_insert(cache->zone_map, id_ptr, zp);
        // dbg_print_g_hash_table("zone_map", cache->zone_map);
        cache->zone_state[zp->zone].chunk_loc++;

        // Full, close the zone now
        if (cache->zone_state[zp->zone].chunk_loc >= cache->max_zone_chunks) {
            if (ze_close_zone(cache, zp->zone) != 0) {
                dbg_printf("Couldn't close zone %u\n", zp->zone);
                // TODO: Should we not fail? What to do with active?
                g_mutex_unlock(&cache->cache_lock);
                return NULL;
            }
            // TODO: Add to LRU queue?
            g_mutex_unlock(&cache->cache_lock);
            return data;
        }

        // Probably this is to reduce lock contention. To allow writes to different zones at once
        // But needs finer-grained locks
        g_queue_push_tail(cache->active_queue, GINT_TO_POINTER(zp->zone)); // Make zone avail again
        dbg_print_g_queue("active_queue", cache->active_queue);
    }

    g_mutex_unlock(&cache->cache_lock);
    return data;
}

/**
 * Validate contents of cache read
 *
 * @param cache Pointer to the `ze_cache` structure.
 * @param data Data to validate against RANDOM_DATA
 * @param id Identifier that should be in first 4 bytes
 * @return Non-zero on error
 */
static int
validate_ze_read(struct ze_cache *cache, unsigned char *data, uint32_t id) {
    uint32_t read_id;
    memcpy(&read_id, data, sizeof(uint32_t));
    if (read_id != id) {
        dbg_printf("Invalid read_id(%u)!=id(%u)\n", read_id, id);
        return -1;
    }
    // 4 bytes for int
    for (uint32_t i = sizeof(uint32_t); i < cache->chunk_sz; i++) {
        if (data[i] != RANDOM_DATA[i]) {
            dbg_printf("data[%d]!=RANDOM_DATA[%d]\n", read_id, id);
            return -1;
        }
    }
    return 0;
}

/**
 * Eviction thread
 *
 * @param user_data thread_data
 * @return
 */
gpointer
evict_task(gpointer user_data) {
    struct ze_thread_data *thread_data = user_data;

    printf("Evict task started by thread %p\n", (void *) g_thread_self());

    while (true) {
        if (thread_data->done) {
            break;
        }

        g_mutex_lock(&thread_data->cache->cache_lock);

        uint32_t free_zones = g_queue_get_length(thread_data->cache->free_list);
        if (free_zones > EVICT_HIGH_THRESH) {
            // Not at mark, wait
            dbg_printf("EVICTION: Free zones=%u > %u, not evicting", free_zones, EVICT_HIGH_THRESH);
            g_mutex_unlock(&thread_data->cache->cache_lock);
            g_usleep(EVICT_SLEEP_US);
            continue;
        }


        g_mutex_unlock(&thread_data->cache->cache_lock);
    }

    printf("Evict task completed by thread %p\n", (void *) g_thread_self());

    return NULL;
}

// Task function. The function that each thread runs. This will simulate servicing cache requests. 
// @param data 
void
task_function(gpointer data, gpointer user_data) {
    (void) user_data;
    struct ze_thread_data *thread_data = data;

    printf("Task %d started by thread %p\n", thread_data->tid, (void *) g_thread_self());
    // ze_print_cache(thread_data->cache);

    // Handles any cache read requests
    while (true) {
        g_mutex_lock(&thread_data->cache->reader.lock);

        // When we exceed Query number, go next workload
        // The query that we're on
        if (thread_data->cache->reader.query_index >= NR_QUERY) {
            thread_data->cache->reader.query_index = 0;
            thread_data->cache->reader.workload_index++;
        }

        // We finished reading all the workloads
        if (thread_data->cache->reader.workload_index >= NR_WORKLOADS) {
            g_mutex_unlock(&thread_data->cache->reader.lock);
            break;
        }

        // Increment the query index
        uint32_t qi = thread_data->cache->reader.query_index++;
        uint32_t wi = thread_data->cache->reader.workload_index;
        g_mutex_unlock(&thread_data->cache->reader.lock);

        // Assertions to validate state
        assert(qi < NR_QUERY);
        assert(wi < NR_WORKLOADS);

        // Find the data in the cache
        unsigned char *data = ze_cache_get(thread_data->cache, simple_workload[wi][qi]);
        if (data == NULL) {
            dbg_printf("ERROR: Couldn't get data\n");
            return;
        }
#ifdef VERIFY
        assert(validate_ze_read(thread_data->cache, data, simple_workload[wi][qi]) == 0);
#endif
        free(data);
        printf("[%d]: ze_cache_get(simple_workload[%d][%d]=%d)\n", thread_data->tid, wi, qi,
               simple_workload[wi][qi]);
    }
    printf("Task %d finished by thread %p\n", thread_data->tid, (void *) g_thread_self());
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
    enum ze_backend device_type = zbd_device_is_zoned(device)?
                                  ZE_BACKEND_ZNS :
                                  ZE_BACKEND_BLOCK;
    size_t chunk_sz = strtoul(argv[2], NULL, 10);
    int32_t nr_threads = strtol(argv[3], NULL, 10);
    int32_t nr_eviction_threads = 1;

    printf("Running with configuration:\n"
           "\tDevice name: %s\n"
           "\tDevice type: %s\n"
           "\tChunk size: %lu\n"
           "\tWorker threads: %u\n"
           "\tEviction threads: %u\n",
           device, (device_type == ZE_BACKEND_ZNS)?
                   "ZNS" : "Block",
           chunk_sz, nr_threads, nr_eviction_threads);

#ifdef DEBUG
    printf("\tDEBUG=on\n");
#endif
#ifdef VERIFY
    printf("\tVERIFY=on\n");
#endif

    struct zbd_info info = {0};
    int fd;
    if (device_type == ZE_BACKEND_ZNS) {
        fd = zbd_open(device, O_RDWR, &info);
    } else {
        fd = open(device, O_RDWR);

        uint64_t size = 0;
        if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
            fprintf(stderr, "Couldn't get block size: %s\n", device);
            return -1;
        }

        info.nr_zones = ((long)size / BLOCK_ZONE_CAPACITY);
        info.max_nr_active_zones = 0;
    }
    
    if (fd < 0) {
        fprintf(stderr, "Error opening device: %s\n", device);
        return fd;
    }

    uint64_t zone_capacity = 0;
    if (device_type == ZE_BACKEND_ZNS) {
        int ret = zbd_reset_zones(fd, 0, 0);
        if (ret != 0) {
            fprintf(stderr, "Couldn't reset zones\n");
            return -1;
        }

        ret = zone_cap(fd, &zone_capacity);
        if (ret != 0) {
            fprintf(stderr, "Couldn't report zone info\n");
            return ret;
        }
    } else {
        zone_capacity = BLOCK_ZONE_CAPACITY;
    }

    RANDOM_DATA = generate_random_buffer(chunk_sz);
    if (RANDOM_DATA == NULL) {
        nomem();
    }

    struct ze_cache cache = {0};
    ze_init_cache(&cache, &info, chunk_sz, zone_capacity, fd, ZE_EVICT_ZONE, device_type);

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
        thread_data[i].cache = &cache;
        g_thread_pool_push(pool, &thread_data[i], &error);
        if (error) {
            fprintf(stderr, "Error pushing task: %s\n", error->message);
            return 1;
        }
    }

    // Setup eviction thread
    struct ze_thread_data eviction_thread_data = {
        .tid = 0, .cache = &cache, .done = false
    };

    GThread *thread = g_thread_new(
        "evict-thread",
        evict_task,
        &eviction_thread_data
    );

    // Wait for tasks to finish and free the thread pool
    g_thread_pool_free(pool, FALSE, TRUE);

    // Notify GC thread we are done
    eviction_thread_data.done = true;

    g_thread_join(thread);

    // Cleanup
    ze_destroy_cache(&cache);
    g_free(thread_data);
    free(RANDOM_DATA);
    return 0;
}
