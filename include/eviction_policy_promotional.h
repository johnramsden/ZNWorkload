#pragma once

#include "eviction_policy.h"
#include "glib.h"

#include <stdint.h>

typedef void *policy_data_t;

struct zn_policy_promotional {
    GQueue lru_queue;            /**< Least Recently Used (LRU) queue for zone eviction. */
    GHashTable *zone_to_lru_map; /**< Hash table mapping zones to locations in the LRU queue. */
    GMutex policy_mutex;         /**< LRU lock */

    uint32_t num_zones; /**< Number of zones */
};
