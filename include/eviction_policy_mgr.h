#ifndef EVICTION_POLICY_MGR_H
#define EVICTION_POLICY_MGR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @enum io_type
 * @brief Describes the type of I/O operation performed.
 */
enum io_type {
    IO_TYPE_READ, /**< A read operation. */
    IO_TYPE_WRITE /**< A write operation. */
};

/**
 * @struct eviction_policy_mgr
 * @brief Manages the policy of which zones (or chunks) to evict.
 *
 * In a zone-level eviction policy:
 *   - Only full zones are considered for eviction.
 *   - Active zones remain out of scope until they become full.
 */
struct eviction_policy_mgr {
    /* Implementation-specific fields go here.
     * Possibly a queue for LRU tracking, or a priority structure, etc.
     */
};

/**
 * @brief Updates the eviction policy with information from a completed I/O operation.
 *
 * @param epm         Pointer to the eviction_policy_mgr.
 * @param zone_id     The zone where the operation took place.
 * @param chunk_idx   The chunk index in that zone.
 * @param type        The type of I/O operation (read or write).
 *
 * Implementations might promote a zone in an LRU queue upon a read, or might add
 * a newly full zone to a 'ready to evict' list upon a write that fills the zone.
 */
void
eviction_policy_mgr_update_eviction(struct eviction_policy_mgr *epm, uint32_t zone_id,
                                    uint32_t chunk_idx, enum io_type type);

/**
 * @brief Makes an eviction decision for a single zone-level eviction policy.
 *
 * @param epm Pointer to the eviction_policy_mgr.
 *
 * @return The ID of the zone to evict, or a sentinel value (e.g., UINT32_MAX)
 *         if no eviction is needed.
 *
 * This function is typically called by an eviction thread that periodically
 * checks if conditions (e.g., free zone threshold) warrant eviction.
 */
uint32_t
eviction_policy_mgr_make_eviction_decision(struct eviction_policy_mgr *epm);

/**
 * @brief Makes a chunk-level eviction decision for GC or advanced policies.
 *
 * @param epm Pointer to the eviction_policy_mgr.
 *
 * @note This function is optional and only needed if you have chunk-level
 *       eviction logic. It might create a list of invalidated chunks
 *       that a GC thread will subsequently relocate.
 */
void
eviction_policy_mgr_make_eviction_decision_chunks(struct eviction_policy_mgr *epm);

/**
 * @brief Performs a garbage collection pass, e.g., relocating valid chunks from
 *        zones that are partially invalidated.
 *
 * @param epm Pointer to the eviction_policy_mgr.
 *
 * @note This function is also optional and only needed if your eviction strategy
 *       separates out the GC step. It might copy valid data to a new zone, then
 *       mark the old zone as free.
 */
void
eviction_policy_mgr_make_gc_decision(struct eviction_policy_mgr *epm);

#endif /* EVICTION_POLICY_MGR_H */
