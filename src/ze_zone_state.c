#include "ze_zone_state.h"

struct ze_pair
ze_get_active_zone() {
    return (struct ze_pair){};
}

GArray
ze_get_active_zone_batch(int chunks) {
    (void) chunks;
    return (GArray){};
}


void
ze_evict(int zone_to_free) {
    (void) zone_to_free;
}

int
ze_get_num_active_zones() {
    return 0;
}

int
ze_get_num_free_zones() {
    return 0;
}

int
ze_get_num_full_zones() {
    return 0;
}
