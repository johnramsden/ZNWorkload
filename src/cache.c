// For pread
#define _XOPEN_SOURCE 500
#include <unistd.h>

#include "znutil.h"
#include "zncache.h"


#include <assert.h>
#include <linux/fs.h>

#include "libzbd/zbd.h"
#include <inttypes.h>

void
zn_fg_evict(struct zn_cache *cache) {
    uint32_t free_zones = zsm_get_num_free_zones(&cache->zone_state);
    if (cache->eviction_policy.type == ZN_EVICT_PROMOTE_ZONE) {
        for (uint32_t i = 0; i < EVICT_LOW_THRESH_ZONES - free_zones; i++) {
            int zone =
                cache->eviction_policy.do_evict(cache->eviction_policy.data);
            if (zone == -1) {
                dbg_printf("No zones to evict%s", "\n");
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
    } else if (cache->eviction_policy.type == ZN_EVICT_CHUNK) {
        (void)cache->eviction_policy.do_evict(cache->eviction_policy.data);
    } else {
        assert(!"NYI");
    }
}

unsigned char *
zn_cache_get(struct zn_cache *cache, const uint32_t id, unsigned char *random_buffer) {
    unsigned char *data = NULL;

    struct zone_map_result result = zn_cachemap_find(&cache->cache_map, id);

    // Found the entry, read it from disk, update eviction, and decrement reader.
    if (result.type == RESULT_LOC) {
        unsigned char *data = zn_read_from_disk(cache, &result.value.location);
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
                zn_fg_evict(cache);
            } else {
                break;
            }
        }

        // Emulates pulling in data from a remote source by filling in a cache entry with random
        // bytes
        data = zn_gen_write_buffer(cache, id, random_buffer);

        // Write buffer to disk, 4kb blocks at a time
        unsigned long long wp =
            CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, location.chunk_offset, location.zone);
        if (zn_write_out(cache->fd, cache->chunk_sz, data, WRITE_GRANULARITY, wp) != 0) {
            dbg_printf("Couldn't write to fd at wp=%llu\n", wp);
            goto UNDO_ZONE_GET;
        }

        // Update metadata
        zsm_return_active_zone(&cache->zone_state, &location);

        cache->eviction_policy.update_policy(cache->eviction_policy.data, location, ZN_WRITE);

        zn_cachemap_insert(&cache->cache_map, id, location);

        return data;

    UNDO_ZONE_GET:
        zsm_failed_to_write(&cache->zone_state, location);
    UNDO_MAP:
        zn_cachemap_fail(&cache->cache_map, id);

        return NULL;
    }
}

void
zn_init_cache(struct zn_cache *cache, struct zbd_info *info, size_t chunk_sz, uint64_t zone_cap,
              int fd, enum zn_evict_policy_type policy, enum zn_backend backend, uint32_t* workload_buffer, uint64_t workload_max) {
    cache->fd = fd;
    cache->chunk_sz = chunk_sz;
    cache->nr_zones = info->nr_zones;
    cache->zone_cap = zone_cap;
    cache->max_nr_active_zones =
        info->max_nr_active_zones == 0 ? MAX_OPEN_ZONES : info->max_nr_active_zones;
    cache->zone_cap = zone_cap;
    cache->max_zone_chunks = zone_cap / chunk_sz;
    cache->backend = backend;
    cache->active_readers = malloc(sizeof(gint) * cache->nr_zones);
    cache->reader.workload_buffer = workload_buffer;
    cache->reader.workload_max = workload_max;

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
    zn_evict_policy_init(&cache->eviction_policy, policy, cache);
    zsm_init(&cache->zone_state, cache->nr_zones, fd, zone_cap, chunk_sz,
             cache->max_nr_active_zones, cache->backend);

    g_mutex_init(&cache->reader.lock);
    cache->reader.workload_index = 0;

    /* VERIFY_ZE_CACHE(cache); */
}

void
zn_destroy_cache(struct zn_cache *cache) {
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

unsigned char *
zn_read_from_disk(struct zn_cache *cache, struct zn_pair *zone_pair) {
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

int
zn_write_out(int fd, size_t to_write, const unsigned char *buffer, ssize_t write_size,
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
            dbg_printf("Couldn't write to fd=%d\n", fd);
            return -1;
        }
        total_written += bytes_written;
        // dbg_printf("total_written=%ld bytes of %zu\n", total_written, to_write);
    }
    return 0;
}

unsigned char *
zn_gen_write_buffer(struct zn_cache *cache, uint32_t zone_id, unsigned char *buffer) {
    unsigned char *data = malloc(cache->chunk_sz);
    if (data == NULL) {
        nomem();
    }

    memcpy(data, buffer, cache->chunk_sz);
    // Metadata
    memcpy(data, &zone_id, sizeof(uint32_t));

    g_usleep(ZE_READ_SLEEP_US);

    return data;
}

int
zn_validate_read(struct zn_cache *cache, unsigned char *data, uint32_t id, unsigned char *compare_buffer) {
    uint32_t read_id;
    memcpy(&read_id, data, sizeof(uint32_t));
    if (read_id != id) {
        dbg_printf("Invalid read_id(%u)!=id(%u)\n", read_id, id);
        return -1;
    }
    // 4 bytes for int
    for (uint32_t i = sizeof(uint32_t); i < cache->chunk_sz; i++) {
        if (data[i] != compare_buffer[i]) {
            dbg_printf("data[%d]!=RANDOM_DATA[%d]\n", read_id, id);
            return -1;
        }
    }
    return 0;
}