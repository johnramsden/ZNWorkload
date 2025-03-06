#pragma once

#include "eviction_policy.h"
#include "glib.h"

#include <stdint.h>

struct zn_policy_promotional {
    // Tail is the end of the queue, head is the least recently used
    GQueue lru_queue;            /**< Least Recently Used (LRU) queue for zone eviction. */
    GHashTable *zone_to_lru_map; /**< Hash table mapping zones to locations in the LRU queue. */
    GMutex policy_mutex;         /**< LRU lock */

    uint32_t zone_max_chunks; /**< Number of chunks in a zone */
};

/** @brief Updates the promotional LRU policy
 */
void
zn_policy_promotional_update(policy_data_t policy, struct zn_pair location,
                             enum zn_io_type io_type);

/** @brief Gets a zone to evict.
    @returns the zone to evict, -1 if there are no full zones.
 */
int
zn_policy_promotional_get_zone_to_evict(policy_data_t policy);
