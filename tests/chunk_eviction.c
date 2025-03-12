#include <assert.h>
#include <libzbd/zbd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "eviction_policy.h"
#include "znutil.h"
#include "zncache.h"
#include "cachemap.h"
#include "zone_state_manager.h"

#define ACTIVE_READERS 1
#define CHUNK_SIZE 524288
#define NUM_DEV 2

char *devices[NUM_DEV] = {
    "/dev/nullb0", // ZNS
    "/dev/nullb1" // Conv
};

struct cache_config {
    uint32_t max_zone_chunks;
    uint32_t max_nr_active_zones;
    struct zn_cachemap cache_map;
    struct zn_evict_policy eviction_policy;
    struct zone_state_manager zone_state;
};

struct config {
    enum zn_backend backend;
    int fd;
    struct zbd_info info;
    uint64_t zone_capacity;
    struct cache_config cache_config;
};

void
setup_cache(struct config *cfg) {
    cfg->cache_config.max_nr_active_zones =
        cfg->info.max_nr_active_zones == 0 ? MAX_OPEN_ZONES : cfg->info.max_nr_active_zones;
    cfg->cache_config.max_zone_chunks = cfg->zone_capacity / CHUNK_SIZE;

    gint active_readers[ACTIVE_READERS];

    // Set up the data structures
    zn_cachemap_init(&cfg->cache_config.cache_map, cfg->info.nr_zones, active_readers);
    zn_evict_policy_init(&cfg->cache_config.eviction_policy, ZN_EVICT_CHUNK, cfg->cache_config.max_zone_chunks, cfg->info.nr_zones,
        &cfg->cache_config.cache_map, &cfg->cache_config.zone_state);
    zsm_init(&cfg->cache_config.zone_state, cfg->info.nr_zones, cfg->fd, cfg->zone_capacity, CHUNK_SIZE,
             cfg->cache_config.max_nr_active_zones, cfg->backend);


}

int
setup_dev(char *device, struct config *cfg) {

    cfg->backend = zbd_device_is_zoned(device) ? ZE_BACKEND_ZNS : ZE_BACKEND_BLOCK;
    if (cfg->backend == ZE_BACKEND_ZNS) {
        cfg->fd = zbd_open(device, O_RDWR, &cfg->info);

        int ret = zbd_reset_zones(cfg->fd, 0, 0);
        if (ret != 0) {
            fprintf(stderr, "Couldn't reset zones\n");
            return -1;
        }

        ret = zone_cap(cfg->fd, &cfg->zone_capacity);
        if (ret != 0) {
            fprintf(stderr, "Error: Couldn't report zone info\n");
            return ret;
        }
    } else {
        cfg->fd = open(device, O_RDWR);

        uint64_t size = 0;
        if (ioctl(cfg->fd, BLKGETSIZE64, &size) == -1) {
            fprintf(stderr, "Error: Couldn't get block size: %s\n", device);
            return -1;
        }

        if (size < BLOCK_ZONE_CAPACITY) {
            fprintf(stderr, "Error: The size of the disk is smaller than a single zone!\n");
            return -1;
        }
        cfg->info.nr_zones = ((long) size / BLOCK_ZONE_CAPACITY);
        cfg->info.max_nr_active_zones = 0;

        cfg->zone_capacity = BLOCK_ZONE_CAPACITY;
    }

    if (cfg->fd < 0) {
        fprintf(stderr, "Error opening device: %s\n", device);
        return cfg->fd;
    }

    return 0;
}


int main(void) {
    int failures = 0;

    struct config cfg[NUM_DEV];
    for (int i = 0; i < NUM_DEV; i++) {
        if (setup_dev(devices[i], &cfg[i]) != 0) {
            fprintf(stderr, "Error: Couldn't setup device %s\n", devices[i]);
            return 1;
        }
        setup_cache(&cfg[i]);
    }

    return failures;
}