#include "ze_cache_map.h"

struct zone_map_result
ze_cache_map_find(int data_id) {
    (void) data_id;
    return (struct zone_map_result){};
}

void
ze_cache_map_insert(int data_id, struct ze_pair location) {
    (void) data_id;
    (void) location;
}

void
ze_clear_zone(int zone) {
    (void) zone;
}
