#include <assert.h>
#include <libzbd/zbd.h>
#include "eviction_policy.h"
#include "znutil.h"
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/fs.h>

#define NUM_DEV 2

char *devices[NUM_DEV] = {
    "/dev/nullb0", // ZNS
    "/dev/nullb1" // Conv
};

struct config {
    bool is_zoned[NUM_DEV];
    int fd[NUM_DEV];
    struct zbd_info info[NUM_DEV];
    uint64_t zone_capacity[NUM_DEV];
};

int setup(char **devices, struct config *cfg) {

    for (int i = 0; i < NUM_DEV; i++) {
        cfg->is_zoned[i] = zbd_device_is_zoned(devices[i]);
        if (cfg->is_zoned[i]) {
            cfg->fd[i] = zbd_open(devices[i], O_RDWR, &cfg->info[i]);

            int ret = zbd_reset_zones(cfg->fd[i], 0, 0);
            if (ret != 0) {
                fprintf(stderr, "Couldn't reset zones\n");
                return -1;
            }

            ret = zone_cap(cfg->fd[i], &cfg->zone_capacity[i]);
            if (ret != 0) {
                fprintf(stderr, "Error: Couldn't report zone info\n");
                return ret;
            }
        } else {
            cfg->fd[i] = open(devices[i], O_RDWR);

            uint64_t size = 0;
            if (ioctl(cfg->fd[i], BLKGETSIZE64, &size) == -1) {
                fprintf(stderr, "Error: Couldn't get block size: %s\n", devices[i]);
                return -1;
            }

            if (size < BLOCK_ZONE_CAPACITY) {
                fprintf(stderr, "Error: The size of the disk is smaller than a single zone!\n");
                return -1;
            }
            cfg->info[i].nr_zones = ((long) size / BLOCK_ZONE_CAPACITY);
            cfg->info[i].max_nr_active_zones = 0;

            cfg->zone_capacity[i] = BLOCK_ZONE_CAPACITY;
        }

        if (cfg->fd[i] < 0) {
            fprintf(stderr, "Error opening device: %s\n", devices[i]);
            return cfg->fd[i];
        }
    }

    return 0;
}

int main(void) {
    int failures = 0;

    struct config cfg;

    if (setup(devices, &cfg) != 0) {
        fprintf(stderr, "Error: Couldn't setup devices\n");
        return 1;
    }

    return failures;
}