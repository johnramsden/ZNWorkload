#include "cachemap.h"
#include "util.h"

void cachemap_init(struct cachemap *cm) {
    cm->zone_id_to_data_id = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (cm->zone_id_to_data_id == NULL) {
        nomem();
    }
    cm->data_id_to_zone = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (cm->data_id_to_zone == NULL) {
        nomem();
    }
}

void cachemap_destroy(struct cachemap *cm) {
    g_hash_table_destroy(cm->zone_id_to_data_id);
    g_hash_table_destroy(cm->data_id_to_zone);
}

struct cachemap_get_result
cachemap_get(struct cachemap *cm, uint32_t data_id) {

}

void
cachemap_insert(struct cachemap *cm, uint32_t data_id, struct zone_pair zone_pair) {

}

void
cachemap_clear_zone(struct cachemap *cm, uint32_t zone_id) {

}