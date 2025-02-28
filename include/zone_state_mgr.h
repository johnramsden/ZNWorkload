#ifndef ZONE_STATE_MGR_H
#define ZONE_STATE_MGR_H

#include <stdint.h>

/**
 * @struct zone_state_mgr
 * @brief Manages the state of all zones (active, free, full).
 *
 * Invariants:
 *   - The total number of zones = active_zones + free_zones + full_zones
 *   - Valid operations on zones include:
 *     1. Activate: (Active <- Free)
 *     2. Full:     (Full <- Active)
 *     3. Evict:    (Free <- Full)
 *   - No writes occur to zones in the Full list (except possibly the last chunk).
 */
struct zone_state_mgr {
    /* Implementation-specific fields go here.
     * Possibly:
     *   - Lists or queues tracking active/free/full zones
     *   - Counters, locks, etc.
     */
};

/**
 * @brief Returns an active zone (and chunk pointer) for writing. If no active zone
 *        is available, moves one from the Free list to the Active list.
 *
 * @param zsm  Pointer to the zone_state_mgr structure.
 *
 * @return A @ref zone_pair containing the zone ID and the next chunk index
 *         available for writing. If the chunk index is at the end, the zone
 *         should be moved to the Full list.
 */
struct zone_pair
zone_state_mgr_get_active_zone(struct zone_state_mgr *zsm);

/**
 * @brief Provides a batch of chunks for a host-side GC or other bulk writes.
 *
 * @param zsm           Pointer to the zone_state_mgr structure.
 * @param nr_chunks_req The number of chunks requested.
 * @param out_pairs     A pointer to an array of @ref zone_pair to be filled in.
 * @param max_pairs     The maximum number of zone_pair elements that @p out_pairs can hold.
 *
 * @return The number of zone_pair elements actually populated in @p out_pairs.
 *
 * This function attempts to gather enough active zone space to accommodate
 * @p nr_chunks_req chunks. If necessary, zones may be moved from the Free list
 * to the Active list.
 */
uint32_t
zone_state_mgr_get_active_zone_batch(struct zone_state_mgr *zsm, uint32_t nr_chunks_req,
                                     struct zone_pair *out_pairs, uint32_t max_pairs);

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
zone_state_mgr_evict(struct zone_state_mgr *zsm, uint32_t zone_id);

/**
 * @brief Returns the total number of zones managed.
 *
 * @param zsm Pointer to the zone_state_mgr structure.
 *
 * @return The total number of zones.
 */
uint32_t
zone_state_mgr_get_zone_count(struct zone_state_mgr *zsm);

#endif // ZONE_STATE_MGR_H
