#include "zone_state_manager.h"
#include "glib.h"
#include "assert.h"
#include "libzbd/zbd.h"
#include "znutil.h"
#include <stdint.h>
#include <stdlib.h>

/**
 * @brief Close a zone
 *
 * @param cache cache Pointer to the `ze_cache` structure, caller is responsible for locking
 * @param zone_id Zone to close
 *
 * @return Returns 0 on success and -1 otherwise.
 */
static int
ze_close_zone(struct ze_zone_state *state, struct ze_zone *zone) {
    if (zone->state == ZE_ZONE_FULL) {
        dbg_printf("Zone already closed\n");
        return 0;
    }

    unsigned long long wp = CHUNK_POINTER(state->zone_cap, state->chunk_size, 0, zone->zone_id);
    dbg_printf("Closing zone %u, zone pointer %llu\n", zone->zone_id, wp);
    zbd_set_log_level(ZBD_LOG_ERROR);

    // FOR DEBUGGING ZONE STATE
    // struct zbd_zone zone;
    // unsigned int nr_zones;
    // if (zbd_report_zones(cache->fd, wp, 1, ZBD_RO_ALL, &zone, &nr_zones) == 0) {
    //     printf("Zone state before close: %u\n", zone.cond == ZBD_ZONE_COND_FULL);
    // }

    // NOTE: FULL ZONES ARE NOT ACTIVE
    int ret = zbd_finish_zones(state->fd, wp, state->zone_cap);
    if (ret != 0) {
        dbg_printf("Failed to close zone %u\n", zone->zone_id);
        return ret;
    }
    // EXPLICIT CLOSE FAILS ON NULLBLK, TODO: TEST ON REAL DEV ON CORTES
    // ret = zbd_close_zones(cache->fd, wp, cache->zone_cap);
    // if (ret != 0) {
    //     return ret;
    // }
    zone->state = ZE_ZONE_FULL;
    zone->chunk_offset = 0;

    return ret;
}

/**
 * @brief Reset a zone
 *
 * @param state Pointer to the `ze_zone_state` structure, caller is responsible for locking
 * @param zone Zone to reset
 *
 * @return Returns 0 on success and -1 otherwise.
 */
static int
ze_reset_zone(struct ze_zone_state *state, struct ze_zone *zone) {
    if (zone->state == ZE_ZONE_FREE) {
        dbg_printf("Zone already closed\n");
        return 0;
    }

    unsigned long long wp = CHUNK_POINTER(state->zone_cap, state->chunk_size, 0, zone->zone_id);
    dbg_printf("Closing zone %u, zone pointer %llu\n", zone->zone_id, wp);
    zbd_set_log_level(ZBD_LOG_ERROR);

    // NOTE: FULL ZONES ARE NOT ACTIVE
    int ret = zbd_reset_zones(state->fd, wp, state->zone_cap);
    if (ret != 0) {
        dbg_printf("Failed to close zone %u\n", zone->zone_id);
        return ret;
    }

    zone->state = ZE_ZONE_FREE;
    zone->chunk_offset = 0;
    g_queue_push_tail(state->free, zone);

    return ret;
}

/**
 * @brief Opens the free zone
 *
 * @param state the zone state
 * @param zone_id Zone to open
 *
 * @note assumes that the lock is held
 *
 * @return Returns 0 on success and -1 otherwise.
 */
static int
ze_open_zone(struct ze_zone_state *state, struct ze_zone *zone) {
    assert(state);
    assert(zone);
    assert(zone->state == ZE_ZONE_FREE);

    if (ze_get_num_active_zones(state) >= state->max_nr_active_zones) {
        dbg_printf("Already at active zone limit\n");
        return -1;
    }

    unsigned long long wp = CHUNK_POINTER(state->zone_cap, state->chunk_size, 0, zone->zone_id);
    dbg_printf("Opening zone %u, zone pointer %llu\n", zone->zone_id, wp);

    int ret = zbd_open_zones(state->fd, wp, 1);
    if (ret != 0) {
        return ret;
    }
    
    zone->state = ZE_ZONE_ACTIVE;
    zone->chunk_offset = 0;
    g_queue_push_tail(state->active, zone);

    return 0;
}

void
zone_state_setup(struct ze_zone_state *state, const uint32_t num_zones,
                 const int fd, const uint64_t zone_cap, const size_t chunk_size, const uint32_t max_nr_active_zones) {
    assert(state);
	state->fd = fd;
    state->zone_cap = zone_cap;
    state->chunk_size = chunk_size;
	state->max_zone_chunks = zone_cap / chunk_size;
	state->max_nr_active_zones = max_nr_active_zones;
	state->writes_occurring = 0;
	state->num_zones = num_zones;

	g_mutex_init(&state->state_mutex);

    state->active = g_queue_new();
    assert(state->active);

	state->free = g_queue_new();
    state->state = calloc(num_zones, sizeof(struct ze_zone));
    assert(state->free);
	assert(state->state);
    for (uint32_t i = 0; i < num_zones; i++) {
        state->state[i] = (struct ze_zone) {.state = ZE_ZONE_FREE, .zone_id = i, .chunk_offset = 0};
        g_queue_push_tail(state->free, &state->state[i]);
    }
}

int
ze_get_active_zone(struct ze_zone_state *state, struct ze_pair *pair) {
    assert(state);
    assert(pair);

	g_mutex_lock(&state->state_mutex);

    uint32_t active_queue_size = g_queue_get_length(state->active);
    uint32_t writer_size = state->writes_occurring;
    uint32_t free_queue_size = ze_get_num_free_zones(state);

    // Perform foreground eviction
    if ((active_queue_size + writer_size) == 0 && free_queue_size == 0) {
        // TODO, need to figure out how to call eviction stuff from
        // here, and ensure deadlocks are avoided. May need to store
        // the cache or other subsystems here... bad design? Could
        // also return failure and tell the caller that it needs to do
        // eviction
        assert(!"TODO: Foreground eviction");
    }

	assert((active_queue_size + writer_size) > 0 || free_queue_size > 0);

    // No active zones that we can use
    if (active_queue_size == 0) {

		// Open a new zone if we can
		if (ze_get_num_active_zones(state) < state->max_nr_active_zones && free_queue_size > 0) {
			struct ze_zone *new_zone = g_queue_pop_head(state->free);
			assert(new_zone->state == ZE_ZONE_FREE);
			int ret = ze_open_zone(state, new_zone);
			if (!ret) {
				g_mutex_unlock(&state->state_mutex);
				return ret;
			}

		} else {
			// The thread needs to wait for a free zone
			g_mutex_unlock(&state->state_mutex);
			return -1;
		}
	}

	// Get an active zone
	dbg_print_g_queue("active,queue", state->active);
	struct ze_zone *active_pair = g_queue_pop_head(state->active);
	assert(active_pair->state == ZE_ZONE_ACTIVE);

	*pair = (struct ze_pair) {
		.zone = active_pair->zone_id,
		.chunk_offset = active_pair->chunk_offset
	};

	active_pair->state = ZE_ZONE_WRITE_OCCURING;
	state->writes_occurring++;

	g_mutex_unlock(&state->state_mutex);
    return 0;
}

// TODO
GArray
ze_get_active_zone_batch(int chunks) {
    (void) chunks;
    return (GArray){};
}

int
ze_return_active_zone(struct ze_zone_state *state, struct ze_pair *pair) {
    assert(state);
    assert(pair);

    g_mutex_lock(&state->state_mutex);
	assert(ze_get_num_active_zones(state) <= state->max_nr_active_zones);

	struct ze_zone *zone = &state->state[pair->zone];
	assert(zone->state == ZE_ZONE_WRITE_OCCURING);
	assert(zone->chunk_offset == pair->chunk_offset);

    // Update the state of the chunk
    state->writes_occurring--;
    zone->chunk_offset++;
    if (zone->chunk_offset == state->max_zone_chunks) {
		ze_close_zone(state, zone);
    } else {
        zone->state = ZE_ZONE_ACTIVE;
		g_queue_push_tail(state->active, zone);
	}

	g_mutex_unlock(&state->state_mutex);
	return 0;
}

int
ze_evict(struct ze_zone_state *state, int zone_to_free) {
	assert(state);

	g_mutex_lock(&state->state_mutex);

	struct ze_zone *zone = &state->state[zone_to_free];
    assert(zone->state == ZE_ZONE_FULL);

    int ret = ze_reset_zone(state, zone);
    if (!ret) {
        g_mutex_unlock(&state->state_mutex);
        return ret;
    }

	assert(zone->state == ZE_ZONE_FREE);

	g_mutex_unlock(&state->state_mutex);
	return 0;
}

uint32_t
ze_get_num_active_zones(struct ze_zone_state *state) {
    g_mutex_lock(&state->state_mutex);
    uint32_t len = g_queue_get_length(state->active) + state->writes_occurring;
    g_mutex_unlock(&state->state_mutex);    
    return len;
}

uint32_t
ze_get_num_free_zones(struct ze_zone_state *state) {
    g_mutex_lock(&state->state_mutex);
    uint32_t len = g_queue_get_length(state->free);
    g_mutex_unlock(&state->state_mutex);
	return len;
}

uint32_t
ze_get_num_full_zones(struct ze_zone_state *state) {
	
    g_mutex_lock(&state->state_mutex);
    uint32_t count = 0;
    for (uint32_t i = 0; i < state->num_zones; i++) {
        if (state->state[i].state == ZE_ZONE_FULL) {
            count++;
        }
    }
    g_mutex_unlock(&state->state_mutex);

    return count;
}
