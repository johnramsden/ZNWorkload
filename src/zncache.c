// For pread
#include <bits/posix1_lim.h>
#define _XOPEN_SOURCE 500
#include <string.h>
#include "zncache.h"

#include "eviction_policy.h"
#include "libzbd/zbd.h"
#include "znutil.h"
#include "zone_state_manager.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <glib.h>
#include <getopt.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define EVICTION_POLICY ZN_EVICT_PROMOTE_ZONE

// BLOCK_ZONE_CAPACITY Defined at compile-time

// No evict

uint32_t simple_workload[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40,
	21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40};

unsigned char *RANDOM_DATA = NULL;

/**
 * @struct zn_thread_data
 * @brief Holds thread-specific data for interacting with the cache.
 *
 * This structure associates a thread with a specific cache instance
 * and provides a unique thread identifier.
 */
struct zn_thread_data {
    struct zn_cache *cache; /**< Pointer to the cache instance associated with this thread. */
    uint32_t tid;           /**< Unique identifier for the thread. */
    bool done;              /**< Marks completed */
};

/**
 * Eviction thread
 *
 * @param user_data thread_data
 * @return
 */
gpointer
evict_task(gpointer user_data) {
    struct zn_thread_data *thread_data = user_data;
    struct zn_cache *cache = thread_data->cache;

    printf("Evict task started by thread %p\n", (void *) g_thread_self());

    while (true) {
        if (thread_data->done) {
            break;
        }

        uint32_t free_zones = zsm_get_num_free_zones(&cache->zone_state);
        if (free_zones > EVICT_HIGH_THRESH_ZONES) {
            g_usleep(EVICT_SLEEP_US);
            continue;
        }

        assert(EVICT_LOW_THRESH_ZONES - free_zones > 0);

        zn_fg_evict(cache);
    }

    printf("Evict task completed by thread %p\n", (void *) g_thread_self());

    return NULL;
}

// Task function. The function that each thread runs. This will simulate servicing cache requests.
// @param data
void
task_function(gpointer data, gpointer user_data) {
    (void) user_data;
    struct zn_thread_data *thread_data = data;

    printf("Task %d started by thread %p\n", thread_data->tid, (void *) g_thread_self());

    // Handles any cache read requests
    while (true) {
        g_mutex_lock(&thread_data->cache->reader.lock);

        uint32_t data_id = 0;

		// When we exceed Query number, go next workload
		// The query that we're on

		// We finished reading all the workloads
		if (thread_data->cache->reader.workload_index >= thread_data->cache->reader.workload_max) {
			g_mutex_unlock(&thread_data->cache->reader.lock);
			break;
		}

		// Increment the query index
		uint32_t wi = thread_data->cache->reader.workload_index;
		g_mutex_unlock(&thread_data->cache->reader.lock);

        printf("[%d]: ze_cache_get(workload[%d]=%d)\n", thread_data->tid, wi,
               thread_data->cache->reader.workload_buffer[wi]);

		data_id = thread_data->cache->reader.workload_buffer[wi];

        // Find the data in the cache
        unsigned char *data = zn_cache_get(thread_data->cache, data_id, RANDOM_DATA);
        if (data == NULL) {
            dbg_printf("ERROR: Couldn't get data for data_id=%u\n", data_id);
            return;
        }
#ifdef VERIFY
        assert(zn_validate_read(thread_data->cache, data, data_id, RANDOM_DATA) == 0);
#endif
        free(data);

        thread_data->cache->reader.workload_index++;
    }
    printf("Task %d finished by thread %p\n", thread_data->tid, (void *) g_thread_self());
}

static void
usage(FILE * file, char *progname) {
    fprintf(file,
            "Usage: %s <DEVICE> <CHUNK_SZ> <THREADS> [-w workload_file] [-i iterations] [-m metrics_file ] [ -h]\n",
            progname);
}

int
main(int argc, char **argv) {
    zbd_set_log_level(ZBD_LOG_ERROR);

    if (geteuid() != 0) {
        fprintf(stderr, "Please run as root\n");
        return -1;
    }

    if (argc < 4 || argc > 11) {
        usage(stderr, argv[0]);
        return -1;
    }

    char *device = argv[1];

    size_t chunk_sz = strtoul(argv[2], NULL, 10);
    int32_t nr_threads = strtol(argv[3], NULL, 10);
    int32_t nr_eviction_threads = 1;

    char *metrics_file = NULL;
    char *workload_file = NULL;
    uint64_t workload_max = UINT64_MAX;
    uint32_t *workload_buffer;
    int c;
    opterr = 0;
    while ((c = getopt(argc, argv, "w:i:m:h")) != -1) {
        switch (c) {
            case 'w':
                workload_file = optarg;
            break;
            case 'i':
                workload_max = strtol(optarg, NULL, 10);
            break;
            case 'm':
                metrics_file = optarg;
            break;
            case 'h':
                usage(stdout, argv[0]);
                exit(EXIT_SUCCESS);
            break;
            case '?':
                if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                }
                usage(stderr, argv[0]);
                return -1;
            default:
                usage(stderr, argv[0]);
                exit(-1);
        }
    }

    enum zn_backend device_type = zbd_device_is_zoned(device) ? ZE_BACKEND_ZNS : ZE_BACKEND_BLOCK;

    if (workload_file != NULL) {
        if (workload_max == UINT64_MAX) {
            fprintf(stderr, "'iterations' must be set if 'workload_file' is set\n");
            return 1;
        }
        int workload_fd = open(workload_file, O_RDONLY);
        if (workload_fd == -1) {
            fprintf(stderr, "Couldn't read workload file %s\n", workload_file);
            return 1;
        }

        size_t workload_sz = workload_max * sizeof(uint32_t);

        workload_buffer = malloc(workload_sz);

        assert(workload_max <= _POSIX_SSIZE_MAX && "Can't be greater than this number");
        errno = 0;
        ssize_t bytes_read = read(workload_fd, workload_buffer, workload_sz);
        if ((size_t)bytes_read != workload_sz) {
            if (errno != 0) {
                fprintf(stderr, "Couldn't read the workload file: '%s'\n", strerror(errno));
            } else {
                fprintf(stderr,
                    "Couldn't read the workload file, iteration number %ld too large, read %ld bytes out of %ld\n",
                    workload_max,  bytes_read, workload_sz);
            }

            return 1;
        }
    } else {
        workload_max = sizeof(simple_workload) / sizeof(uint32_t);
        workload_buffer = simple_workload;
    }

    printf("Running with configuration:\n"
           "\tDevice name: %s\n"
           "\tDevice type: %s\n"
           "\tChunk size: %lu\n"
           "\tBLOCK_ZONE_CAPACITY: %u\n"
           "\tWorker threads: %u\n"
           "\tEviction threads: %u\n"
           "\tWorkload file: %s\n"
           "\tMetrics file: %s\n",
           device, (device_type == ZE_BACKEND_ZNS) ? "ZNS" : "Block", chunk_sz,
           BLOCK_ZONE_CAPACITY, nr_threads, nr_eviction_threads,
           workload_file != NULL ? workload_file : "Simple generator",
           metrics_file != NULL ? metrics_file : "NO");

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

        if (size < BLOCK_ZONE_CAPACITY) {
            assert(!"The size of the disk is smaller than a single zone!");
        }
        info.nr_zones = ((long) size / BLOCK_ZONE_CAPACITY);
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

    struct zn_cache cache = {0};
    zn_init_cache(&cache, &info, chunk_sz, zone_capacity, fd, EVICTION_POLICY, device_type, workload_buffer, workload_max, metrics_file);

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
    struct zn_thread_data *thread_data = g_new(struct zn_thread_data, nr_threads);
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
    struct zn_thread_data eviction_thread_data = {.tid = 0, .cache = &cache, .done = false};

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
    zn_destroy_cache(&cache);
    g_free(thread_data);
    free(RANDOM_DATA);
    return 0;
}
