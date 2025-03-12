#include <assert.h>
#include <libzbd/zbd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "eviction_policy.h"
#include "znutil.h"
#include "cachemap.h"
#include "zone_state_manager.h"

/* FOR USE IN TESTING DEVICES */

#define NUM_DEV 2

char *devices[NUM_DEV] = {
    "/dev/nullb0", // ZNS
    "/dev/nullb1" // Conv
};

struct config {
    bool is_zoned;
    int fd;
    struct zbd_info info;
    uint64_t zone_capacity;
};

int
setup_dev(char *device, struct config *cfg) {

        cfg->is_zoned = zbd_device_is_zoned(device);
        if (cfg->is_zoned) {
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
    }

    return failures;
}