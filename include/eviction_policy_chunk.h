#pragma once

#include "minheap.h"
#include "eviction_policy.h"
#include "glib.h"

#include <stdint.h>

struct eviction_policy_chunk_zone {
    struct zn_pair *chunks; /**< Pool of chunks, backing for lru */
    uint32_t chunks_in_use;
    bool filled;
};

struct zn_policy_chunk {
    // Tail is the end of the queue, head is the least recently used
    GQueue lru_queue;             /**< Least Recently Used (LRU) queue of chunks for eviction. */
    GHashTable *chunk_to_lru_map; /**< Hash table mapping chunks to locations in the LRU queue. */
    GMutex policy_mutex;          /**< LRU lock */

    struct zn_minheap * invalid_pqueue; /**< Priority queue keeping track of invalid zones */

    struct eviction_policy_chunk_zone *zone_pool; /**< Pool of zones, backing for lru */

    uint32_t zone_max_chunks;   /**< Number of chunks in a zone */
};

/** @brief Updates the chunk LRU policy
 */
void
zn_policy_chunk_update(policy_data_t policy, struct zn_pair location,
                             enum zn_io_type io_type);

/** @brief Gets a chunk to evict.
    @returns the zone to evict, -1 if there are no full zones.
 */
int
zn_policy_chunk_get_chunk_to_evict(policy_data_t policy);
