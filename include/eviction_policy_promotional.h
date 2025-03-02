#pragma once

#include "glib.h"
#include <stdint.h>

typedef void* policy_data_t;

struct ze_promotional_policy {
    GQueue lru_queue; /**< Least Recently Used (LRU) queue for zone eviction. */
    GHashTable *zone_to_lru_map; /**< Hash table mapping zones to locations in the LRU queue. */
	GMutex policy_mutex; /**< LRU lock */

	uint32_t num_zones; /**< Number of zones */
};


