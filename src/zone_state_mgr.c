#include "zone_state_mgr.h"

struct zone_pair
zone_state_mgr_get_active_zone(struct zone_state_mgr *zsm) {

}

uint32_t
zone_state_mgr_get_active_zone_batch(struct zone_state_mgr *zsm, uint32_t nr_chunks_req,
                                     struct zone_pair *out_pairs, uint32_t max_pairs) {

}

/**
 * @brief Moves a zone to the Free state, allowing it to be reused.
 *
 * @param zsm     Pointer to the zone_state_mgr structure.
 * @param zone_id The ID of the zone to evict (move from Full to Free).
 *
 * Implementations may clear internal metadata or reset the zone state, so
 * subsequent writes can reuse this zone.
 */
void
zone_state_mgr_evict(struct zone_state_mgr *zsm, uint32_t zone_id) {

}

/**
 * @brief Returns the total number of zones managed.
 *
 * @param zsm Pointer to the zone_state_mgr structure.
 *
 * @return The total number of zones.
 */
uint32_t
zone_state_mgr_get_zone_count(struct zone_state_mgr *zsm) {

}