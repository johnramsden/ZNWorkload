#include "zone-evict-ideal.h"

#include "libzbd/zbd.h"
#include <fcntl.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <glib.h>

#define DEBUG 1

struct zone_pair {
    uint32_t zone;
    uint32_t id;
};

struct zone_addr_map {
    uint32_t nr_zones;
    uint64_t max_zone_chunks;
    size_t chunk_sz;
    uint64_t zone_cap;
    GHashTable *zone_map;
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
gen_workload(struct zone_addr_map * zam, uint32_t num) {

}

static int
init_zone_addr_map(struct zone_addr_map * zam, uint32_t nr_zones, size_t chunk_sz, uint64_t zone_cap) {
    int ret = 0;

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
    zam->zone_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    if (zam->zone_map == NULL) {
        return -1;
    }
    g_hash_table_insert(zam->zone_map, GINT_TO_POINTER(1), GINT_TO_POINTER(100));

    // Retrieve a value by key
    int value = GPOINTER_TO_INT(g_hash_table_lookup(zam->zone_map, GINT_TO_POINTER(1)));
    printf("Value for key %d: %d\n", 1, value);

    return ret;
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

    struct zone_addr_map zam = {0};
    ret = init_zone_addr_map(&zam, info.nr_zones, chunk_sz, zone_capacity);
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize zone address map\n");
        return ret;
    }

    // Cleanup
    if (zam.zone_map != NULL) {
        g_hash_table_destroy(zam.zone_map);
    }
    zbd_close(fd);
    return 0;
}