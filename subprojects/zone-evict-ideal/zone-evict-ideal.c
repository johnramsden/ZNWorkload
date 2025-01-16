#include "zone-evict-ideal.h"

#include <assert.h>

#include "libzbd/zbd.h"
#include <fcntl.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>

#define DEBUG 1
#define VERIFY 1

struct ze_pair {
    uint32_t zone;
    uint32_t chunk_offset;
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
    GMutex lock;
    GHashTable *zone_map;    // Map ID to ze_pair (in lru)
    // Free/active,state lists
    GQueue *free_list;       // List of free zones
    GQueue *active_queue;     // List of active zones
    enum ze_zone_state *zone_state;
};

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

static int
ze_init_cache(struct ze_cache * zam, uint32_t nr_zones, size_t chunk_sz, uint64_t zone_cap, int fd) {
    assert(zam != NULL);

    int ret = 0;

    zam->fd = fd;
    zam->chunk_sz = chunk_sz;
    zam->nr_zones = nr_zones;
    zam->zone_cap = zone_cap;
    zam->max_zone_chunks = zone_cap / chunk_sz;

    if (DEBUG) {
        printf("Initialized address map:\n");
        printf("\tchunk_sz=%lu\n", chunk_sz);
        printf("\tnr_zones=%u\n", nr_zones);
        printf("\tzone_cap=%" PRIu64 "\n", zone_cap);
        printf("\tmax_zone_chunks=%" PRIu64 "\n", zam->max_zone_chunks);
    }

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

    // struct zone_pair *zp = g_new(struct zone_pair, 1);
    // zp->zone = 1;
    // zp->chunk_offset = 2;
    // g_hash_table_insert(zam->zone_map, GINT_TO_POINTER(1), zp);
    //
    // // Retrieve a value by key
    // zp = g_hash_table_lookup(zam->zone_map, GINT_TO_POINTER(1));
    // printf("Value for key %d: %d\n", 1, zp->chunk_offset);

    assert(zam != NULL);
    assert(zam->zone_map != NULL);
    assert(zam->zone_state != NULL);
    assert(zam->active_queue != NULL);
    assert(zam->lru_queue != NULL);

    return ret;
}

static void
ze_destroy_cache(struct ze_cache * zam) {
    // Asserts should always pass
    assert(zam != NULL);
    assert(zam->zone_map != NULL);
    assert(zam->zone_state != NULL);
    assert(zam->active_queue != NULL);
    assert(zam->lru_queue != NULL);
    assert(zam->fd > 0);

    g_hash_table_destroy(zam->zone_map);
    g_free(zam->zone_state);
    g_queue_free_full(zam->active_queue, g_free);
    g_queue_free_full(zam->lru_queue, g_free);
    zbd_close(zam->fd);
}

int
main(int argc, char **argv) {
    zbd_set_log_level(ZBD_LOG_ERROR);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <DEVICE> <CHUNK_SZ>\n", argv[0]);
        return -1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Please run as root\n");
        return -1;
    }

    char *device = argv[1];
    char *chunk_sz_str = argv[2];

    size_t chunk_sz = strtoul(chunk_sz_str, NULL, 10);

    printf("Running with configuration:\n\tDevice name: %s\n\tChunk size: %lu\n", device, chunk_sz);

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

    // Cleanup
    ze_destroy_cache(&zam);
    return 0;
}