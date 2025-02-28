#pragma once

#include "glib.h"
#include "stdbool.h"
#include "ze_cache.h"

/**
 * @struct ze_zone_state
 * @brief Stores the state of a ZNS SSD.
 */
struct ze_zone_state {
    GMutex state_mutex;
    GQueue active;
    GQueue free;
	bool *full;
};

/** @brief Returns a new chunk that a thread can write to
 *  @return the new location to write to 
 *  Implementation notes:
 *  - Gets an active zone if it can, otherwise get from the free list (and move it to the active list)
 *  - Increment the corresponding chunk pointer to point to the next free zone
 *  - If chunk pointer reaches the end, move zone to full list
*/
struct ze_pair
ze_get_active_zone();

/** @brief Not yet thought out well, but a function for host-side gc (when we need to relocate a number of chunks)
 *  @param chunks specifies how many chunks we need
 *  @return which chunks can be written to, in the form of a dynamic array
 *  Implementation notes:
 *  - Updates the zone state accordingly
 */
GArray
ze_get_active_zone_batch(int chunks);

/** @brief Moves full zones to the free zone to make them available again
 *  @param zone_to_free the zone to make free again
 *  Implementation notes
 *  - Should be the one to perform the freeing operation
 *  - Does not manage zone eviction policy
 */ 
void
ze_evict(int zone_to_free);

/** @brief Returns the active zone count */
int
ze_get_num_active_zones();

/** @brief Returns the free zone count */
int
ze_get_num_free_zones();

/** @brief Returns the full zone count */
int
ze_get_num_full_zones();
