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
#define NR_WORKLOADS 2
#define NR_QUERY 20
#define NR_REPEAT_WORKLOAD 2

uint32_t simple_workload[NR_WORKLOADS][NR_QUERY] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}
};

unsigned char *RANDOM_DATA = NULL;

struct ze_pair {
    uint32_t zone;
    uint32_t chunk_offset;
};

struct ze_reader {
    GMutex lock;
    uint32_t query_index;
    uint32_t workload_index;
    uint32_t repeat_number;
};

enum ze_zone_state {
    ZE_ZONE_FREE = 0,
    ZE_ZONE_FULL = 1,
    ZE_ZONE_ACTIVE = 2,
};

struct ze_cache {
    int fd;
    uint32_t nr_zones;
    uint64_t max_zone_chunks;
    size_t chunk_sz;
    uint64_t zone_cap;
    // Cache
    GQueue *lru_queue;       // LRU queue for zones
    GMutex cache_lock;
    GHashTable *zone_map;    // Map ID to ze_pair (in lru)
    // Free/active,state lists
    GQueue *free_list;       // List of free zones
    GQueue *active_queue;     // List of active zones
    enum ze_zone_state *zone_state;
    struct ze_reader reader;
};

struct ze_thread_data {
    struct ze_cache *cache;
    uint32_t tid;
};

unsigned char *generate_random_buffer(size_t size) {
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
 * Uniform rand with limit
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
 * Cite: https://github.com/westerndigitalcorporation/libzbd/blob/master/include/libzbd/zbd.h
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
 * Get zone capacity
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
static void
check_assertions_ze_cache(struct ze_cache * zam) {
    // Asserts should always pass
    assert(zam != NULL);
    assert(zam->zone_map != NULL);
    assert(zam->zone_state != NULL);
    assert(zam->active_queue != NULL);
    assert(zam->lru_queue != NULL);
    assert(zam->fd > 0);
}
#define VERIFY_ZE_CACHE(ptr) check_assertions_ze_cache(ptr)
#else
#define VERIFY_ZE_CACHE(ptr) // Do nothing
#endif

static void
ze_print_cache(struct ze_cache * zam) {
    (void)zam;
#ifdef DEBUG
        printf("\tchunk_sz=%lu\n", zam->chunk_sz);
        printf("\tnr_zones=%u\n", zam->nr_zones);
        printf("\tzone_cap=%" PRIu64 "\n", zam->zone_cap);
        printf("\tmax_zone_chunks=%" PRIu64 "\n", zam->max_zone_chunks);
#endif
}

static int
ze_init_cache(struct ze_cache * zam, uint32_t nr_zones, size_t chunk_sz, uint64_t zone_cap, int fd) {
    int ret = 0;

    zam->fd = fd;
    zam->chunk_sz = chunk_sz;
    zam->nr_zones = nr_zones;
    zam->zone_cap = zone_cap;
    zam->max_zone_chunks = zone_cap / chunk_sz;

#ifdef DEBUG
        printf("Initialized address map:\n");
        printf("\tchunk_sz=%lu\n", chunk_sz);
        printf("\tnr_zones=%u\n", nr_zones);
        printf("\tzone_cap=%" PRIu64 "\n", zone_cap);
        printf("\tmax_zone_chunks=%" PRIu64 "\n", zam->max_zone_chunks);
#endif

    // Create a hash table with integer keys and values
    zam->zone_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    if (zam->zone_map == NULL) {
        return ENOMEM;
    }

    // Init lists:

    // Alloc and set all to FREE
    zam->zone_state = g_new0(enum ze_zone_state, nr_zones);
    if (zam->zone_state == NULL) {
        return ENOMEM;
    }

    zam->lru_queue = g_queue_new();
    if (zam->lru_queue == NULL) {
        return ENOMEM;
    }

    zam->active_queue = g_queue_new();
    if (zam->active_queue == NULL) {
        return ENOMEM;
    }

    g_mutex_init(&zam->cache_lock);
    g_mutex_init(&zam->reader.lock);
    zam->reader.query_index = 0;
    zam->reader.workload_index = 0;
    zam->reader.repeat_number = 1;

    VERIFY_ZE_CACHE(zam);

    return ret;
}

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

static int
ze_cache_get(struct ze_cache * zam, uint32_t id) {
    VERIFY_ZE_CACHE(zam);

    g_mutex_lock(&zam->cache_lock);

    if (g_hash_table_contains(zam->zone_map, GINT_TO_POINTER(id))) {
        printf("Cache ID %u in cache\n", id);
    } else {
        printf("Cache ID not %u in cache\n", id);
        struct ze_pair *zp = g_new(struct ze_pair, 1);
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
        uint32_t qi;
        uint32_t wi;
        g_mutex_lock(&thread_data->cache->reader.lock);
        if (thread_data->cache->reader.query_index >= NR_QUERY) {
            thread_data->cache->reader.query_index = 0;

            if (thread_data->cache->reader.repeat_number < NR_REPEAT_WORKLOAD) {
                thread_data->cache->reader.repeat_number++;
            } else {
                thread_data->cache->reader.repeat_number = 1;
                thread_data->cache->reader.workload_index++;
            }

            if (thread_data->cache->reader.workload_index >= NR_WORKLOADS) {
                g_mutex_unlock(&thread_data->cache->reader.lock);
                break;
            }
        }

        // Assertions to validate state
        assert(thread_data->cache->reader.repeat_number <= NR_REPEAT_WORKLOAD);
        assert(thread_data->cache->reader.query_index < NR_QUERY);
        assert(thread_data->cache->reader.workload_index < NR_WORKLOADS);
        assert(thread_data->cache->reader.repeat_number < NR_REPEAT_WORKLOAD+1);
        assert(thread_data->cache->reader.query_index < NR_QUERY);
        assert(thread_data->cache->reader.workload_index < NR_WORKLOADS);

        qi = thread_data->cache->reader.query_index++;
        wi = thread_data->cache->reader.workload_index;
        g_mutex_unlock(&thread_data->cache->reader.lock);
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
    ret = ze_init_cache(&zam, info.nr_zones, chunk_sz, zone_capacity, fd);
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize zone address map\n");
        return ret;
    }

    RANDOM_DATA = generate_random_buffer(chunk_sz);
    if (RANDOM_DATA == NULL) {
        return ENOMEM;
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