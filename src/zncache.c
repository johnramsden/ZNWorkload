// For pread
#define _XOPEN_SOURCE 500
#include "zncache.h"

#include "cachemap.h"
#include "eviction_policy.h"
#include "eviction_policy_promotional.h"
#include "libzbd/zbd.h"
#include "znutil.h"
#include "zone_state_manager.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define EVICT_HIGH_THRESH 2
#define EVICT_LOW_THRESH 4
#define EVICTION_POLICY ZN_EVICT_PROMOTE_ZONE

#define MICROSECS_PER_SECOND 1000000
#define EVICT_SLEEP_US ((long) (0.5 * MICROSECS_PER_SECOND))
#define ZE_READ_SLEEP_US ((long) (0.25 * MICROSECS_PER_SECOND))

#define MAX_OPEN_ZONES 14
#define WRITE_GRANULARITY 4096

#define BLOCK_ZONE_CAPACITY ((long) 1077 * 1024 * 1024)

// No evict
#define NR_WORKLOADS 4
#define NR_QUERY 20

uint32_t simple_workload[NR_WORKLOADS][NR_QUERY] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},
    {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}};

unsigned char *RANDOM_DATA = NULL;

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

    struct zn_cachemap cache_map;
    struct zn_evict_policy eviction_policy;
    struct zone_state_manager zone_state;
    struct ze_reader reader; /**< Reader structure for tracking workload location. */
    gint *active_readers;    /**< Owning reference of the list of active readers per zone */
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
    volatile struct zbd_zone zone;

    unsigned int nr_zones;

    // See https://github.com/johnramsden/ZNWorkload/issues/12

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
    int ret = zbd_report_zones(fd, ofst, len, ZBD_RO_ALL, &zone, &nr_zones);
    #pragma GCC diagnostic pop
    if (ret != 0) {
        return ret;
    }
    *zone_capacity = zone.capacity;
    return ret;
}

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
              int fd, enum zn_evict_policy_type policy, enum ze_backend backend) {
    cache->fd = fd;
    cache->chunk_sz = chunk_sz;
    cache->nr_zones = info->nr_zones;
    cache->zone_cap = zone_cap;
    cache->max_nr_active_zones =
        info->max_nr_active_zones == 0 ? MAX_OPEN_ZONES : info->max_nr_active_zones;
    cache->nr_active_zones = 0;
    cache->zone_cap = zone_cap;
    cache->max_zone_chunks = zone_cap / chunk_sz;
    cache->backend = backend;
    cache->active_readers = malloc(sizeof(gint) * cache->nr_zones);

#ifdef DEBUG
    printf("Initialized cache:\n");
    printf("\tchunk_sz=%lu\n", cache->chunk_sz);
    printf("\tnr_zones=%u\n", cache->nr_zones);
    printf("\tzone_cap=%" PRIu64 "\n", cache->zone_cap);
    printf("\tmax_zone_chunks=%" PRIu64 "\n", cache->max_zone_chunks);
    printf("\tmax_nr_active_zones=%u\n", cache->max_nr_active_zones);
#endif

    // Set up the data structures
    zn_cachemap_init(&cache->cache_map, cache->nr_zones, cache->active_readers);
    zn_evict_policy_init(&cache->eviction_policy, policy, cache->max_zone_chunks);
    zsm_init(&cache->zone_state, cache->nr_zones, fd, zone_cap, chunk_sz,
             cache->max_nr_active_zones);

    g_mutex_init(&cache->reader.lock);
    cache->reader.query_index = 0;
    cache->reader.workload_index = 0;

    /* VERIFY_ZE_CACHE(cache); */
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
    (void) cache;

    // TODO assert(!"Todo: clean up cache");

    /* g_hash_table_destroy(cache->zone_map); */
    /* g_free(cache->zone_state); */
    /* g_queue_free_full(cache->active_queue, g_free); */
    /* g_queue_free(cache->lru_queue); */

    /* // TODO: MISSING FREES */

    /* if(cache->backend == ZE_BACKEND_ZNS) { */
    /*     zbd_close(cache->fd); */
    /* } else { */
    /*     close(cache->fd); */
    /* } */

    /* g_mutex_clear(&cache->cache_lock); */
    /* g_mutex_clear(&cache->reader.lock); */
}

/**
 * @brief Read a chunk from disk
 *
 * @param cache Pointer to the `ze_cache` structure
 * @param zone_pair Chunk, zone pair
 * @return Buffer read from disk, to be freed by caller
 */
static unsigned char *
ze_read_from_disk(struct ze_cache *cache, struct zn_pair *zone_pair) {
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
 * Simulates remote read with ZE_READ_SLEEP_US
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

    g_usleep(ZE_READ_SLEEP_US);

    return data;
}

static void
fg_evict(struct ze_cache *cache) {
    dbg_printf("EVICTING\n");
    uint32_t free_zones = zsm_get_num_free_zones(&cache->zone_state);
    for (uint32_t i = 0; i < EVICT_LOW_THRESH - free_zones; i++) {
        int zone =
            cache->eviction_policy.get_zone_to_evict(cache->eviction_policy.data);
        if (zone == -1) {
            dbg_printf("No zones to evict\n");
            break;
        }

        zn_cachemap_clear_zone(&cache->cache_map, zone);
        while (cache->active_readers[zone] > 0) {
            g_thread_yield();
        }

        // We can assume that no threads will create entries to the zone in the cache map,
        // because it is full.
        int ret = zsm_evict(&cache->zone_state, zone);
        if (ret != 0) {
            assert(!"Issue occurred with evicting zones\n");
        }
    }
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

    struct zone_map_result result = zn_cachemap_find(&cache->cache_map, id);

    // Found the entry, read it from disk, update eviction, and decrement reader.
    if (result.type == RESULT_LOC) {
        unsigned char *data = ze_read_from_disk(cache, &result.value.location);
        cache->eviction_policy.update_policy(cache->eviction_policy.data, result.value.location,
                                             ZN_READ);

        // Sadly, we have to remember to decrement the reader count here
        g_atomic_int_dec_and_test(&cache->active_readers[result.value.location.zone]);
        return data;
    } else { // result.type == RESULT_COND

        // Repeatedly attempt to get an active zone. This function can fail when there all active
        // zones are writing, so put this into a while loop.
        struct zn_pair location;
        while (true) {

            enum zsm_get_active_zone_error ret = zsm_get_active_zone(&cache->zone_state, &location);

            if (ret == ZSM_GET_ACTIVE_ZONE_RETRY) {
                g_thread_yield();
            } else if (ret == ZSM_GET_ACTIVE_ZONE_ERROR) {
                goto UNDO_MAP;
            } else if (ret == ZSM_GET_ACTIVE_ZONE_EVICT) {
                fg_evict(cache);
            } else {
                break;
            }
        }

        // Emulates pulling in data from a remote source by filling in a cache entry with random
        // bytes
        data = ze_gen_write_buffer(cache, id);

        // Write buffer to disk, 4kb blocks at a time
        unsigned long long wp =
            CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, location.chunk_offset, location.zone);
        if (ze_write_out(cache->fd, cache->chunk_sz, data, WRITE_GRANULARITY, wp) != 0) {
            dbg_printf("Couldn't write to fd at wp=%llu\n", wp);
            goto UNDO_ZONE_GET;
        }

        // Update metadata
        zn_cachemap_insert(&cache->cache_map, id, location);

        zsm_return_active_zone(&cache->zone_state, &location);

        cache->eviction_policy.update_policy(cache->eviction_policy.data, location, ZN_WRITE);

        return data;

    UNDO_ZONE_GET:
        zsm_failed_to_write(&cache->zone_state, location);
    UNDO_MAP:
        zn_cachemap_fail(&cache->cache_map, id);

        return NULL;
    }
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
ze_validate_read(struct ze_cache *cache, unsigned char *data, uint32_t id) {
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
    struct ze_cache *cache = thread_data->cache;

    printf("Evict task started by thread %p\n", (void *) g_thread_self());

    while (true) {
        if (thread_data->done) {
            break;
        }

        uint32_t free_zones = zsm_get_num_free_zones(&cache->zone_state);
        if (free_zones > EVICT_HIGH_THRESH) {
            // Not at mark, wait
            // dbg_printf("EVICTION: Free zones=%u > %u, not evicting\n", free_zones,
            //            EVICT_HIGH_THRESH);
            g_usleep(EVICT_SLEEP_US);
            continue;
        }

        assert(EVICT_LOW_THRESH - free_zones > 0);

        fg_evict(cache);
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
        assert(ze_validate_read(thread_data->cache, data, simple_workload[wi][qi]) == 0);
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
    enum ze_backend device_type = zbd_device_is_zoned(device) ? ZE_BACKEND_ZNS : ZE_BACKEND_BLOCK;
    size_t chunk_sz = strtoul(argv[2], NULL, 10);
    int32_t nr_threads = strtol(argv[3], NULL, 10);
    int32_t nr_eviction_threads = 1;

    printf("Running with configuration:\n"
           "\tDevice name: %s\n"
           "\tDevice type: %s\n"
           "\tChunk size: %lu\n"
           "\tWorker threads: %u\n"
           "\tEviction threads: %u\n",
           device, (device_type == ZE_BACKEND_ZNS) ? "ZNS" : "Block", chunk_sz, nr_threads,
           nr_eviction_threads);

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

        info.nr_zones = ((long) size / BLOCK_ZONE_CAPACITY);
        info.max_nr_active_zones = 0;
        
        dbg_printf("SSD NYI\n"); return 0; // TODO
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
    ze_init_cache(&cache, &info, chunk_sz, zone_capacity, fd, EVICTION_POLICY, device_type);

    GError *error = NULL;
    // Create a thread pool with a maximum of nr_threads
    GThreadPool *pool = g_thread_pool_new(task_function, NULL, nr_threads, FALSE, &error);
    if (error) {
        fprintf(stderr, "Error creating thread pool: %s\n", error->message);
        return 1;
    }

    struct timespec start_time, end_time;
    TIME_NOW(&start_time);

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
    struct ze_thread_data eviction_thread_data = {.tid = 0, .cache = &cache, .done = false};

    GThread *thread = g_thread_new("evict-thread", evict_task, &eviction_thread_data);

    // Wait for tasks to finish and free the thread pool
    g_thread_pool_free(pool, FALSE, TRUE);

    // Notify GC thread we are done
    eviction_thread_data.done = true;

    g_thread_join(thread);

    TIME_NOW(&end_time);

    dbg_printf("Total runtime: %0.2fs (%0.2fms)\n", TIME_DIFFERENCE_SEC(start_time, end_time),
               TIME_DIFFERENCE_MILLISEC(start_time, end_time));

    // Cleanup
    ze_destroy_cache(&cache);
    g_free(thread_data);
    free(RANDOM_DATA);
    return 0;
}
