#include "zone-evict-ideal.h"

#include "libzbd/zbd.h"
#include <fcntl.h>
/**
 * https://github.com/westerndigitalcorporation/libzbd/blob/master/include/libzbd/zbd.h
 */
static void
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

int main(int argc, char **argv) {
    zbd_set_log_level(ZBD_LOG_ERROR);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <DEVICE>\n", argv[0]);
        return -1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Please run as root\n");
        return -1;
    }

    char *device = argv[1];

    struct zbd_info info;
    int fd = zbd_open(device, O_RDWR, &info);
    if (fd < 0) {
        fprintf(stderr, "Error opening device: %s\n", device);
        return fd;
    }

    print_zbd_info(&info);
}