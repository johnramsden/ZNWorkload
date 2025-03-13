#include "cachemap.h"

#include "assert.h"
#include "glib.h"
#include "glibconfig.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void
zn_cachemap_init(struct zn_cachemap *map, const int num_zones, gint *active_readers_arr) {
    g_mutex_init(&map->cache_map_mutex);

    map->zone_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    assert(map->zone_map);

    map->data_map = malloc(sizeof(GArray) * num_zones);
    assert(map->data_map);

    // Zone → Data ID
    for (int i = 0; i < num_zones; i++) {
        map->data_map[i] = g_array_new(FALSE, FALSE, sizeof(uint32_t));
    }

    map->active_readers = active_readers_arr;
}

static void
free_cond_var(GCond *cond) {
    g_cond_clear(cond);
}

struct zone_map_result
zn_cachemap_find(struct zn_cachemap *map, const uint32_t data_id) {
    assert(map);

    g_mutex_lock(&map->cache_map_mutex);

    // Loop for spurious wakeups
    while (true) {

        // We found an entry
        if (g_hash_table_contains(map->zone_map, GINT_TO_POINTER(data_id))) {

            struct zone_map_result *lookup =
                g_hash_table_lookup(map->zone_map, GINT_TO_POINTER(data_id));

            // Releases the lock and waits until it is signalled again
            if (lookup->type == RESULT_COND) {

                // Increment the rc before sleeping on it
                GCond *cond = lookup->value.write_finished;
                g_atomic_rc_box_acquire(cond);
                g_cond_wait(cond, &map->cache_map_mutex);
                g_atomic_rc_box_release_full(cond, (GDestroyNotify) free_cond_var);

                continue;
            } else { // Found the entry, increment reader and return it
                g_atomic_int_inc(&map->active_readers[lookup->value.location.zone]);
                g_mutex_unlock(&map->cache_map_mutex);
                return *lookup;
            }

        } else { // The thread needs to write an entry.

            // Insert a temporary condition for now, and the thread now needs to write it
            struct zone_map_result *wait_cond = g_new0(struct zone_map_result, 1);
            wait_cond->type = RESULT_COND;

            // Create a reference counted condition variable
            wait_cond->value.write_finished = g_atomic_rc_box_new(GCond);
            g_cond_init(wait_cond->value.write_finished);

            g_hash_table_insert(map->zone_map, GINT_TO_POINTER(data_id), wait_cond);
            g_mutex_unlock(&map->cache_map_mutex);
            return *wait_cond;

            // Note that the lifetimes of the condition variable and
            // zone_map_result are different.
            //
            // It is unknown how many threads are waiting for the
            // entry at the same time, so the cv in general cannot be
            // freed deterministically. So we decouple the lifetimes
            // by creating an rc'ed cv.
            //
            // There are two situations in which the cv outlives the
            // zone_map_result. The first situation is when the
            // writing thread calls zn_cachemap_insert and overwrites
            // the entry while there are threads still waiting on the
            // cv. The second situation occurs when the eviction
            // thread erases the zone_map_result during eviction. To
            // avoid checking a freed zone_map_result, threads recheck
            // the entire hash table for the data, not the entry that
            // contained the initial zone_map_result.
        }
    };
}

void
zn_cachemap_insert(struct zn_cachemap *map, const uint32_t data_id, struct zn_pair location) {
    assert(map);

    g_mutex_lock(&map->cache_map_mutex);

    // It must contain an entry if the thread called zn_cachemap_find beforehand
    assert(g_hash_table_contains(map->zone_map, GUINT_TO_POINTER(data_id)));

    struct zone_map_result *result = g_hash_table_lookup(map->zone_map, GUINT_TO_POINTER(data_id));
    assert(result->type == RESULT_COND);

    GCond *condition = result->value.write_finished;
    result->value.location = location; // Does this mutate the entry in the hash table?
    result->type = RESULT_LOC;
    g_array_append_val(map->data_map[location.zone], data_id);
    g_cond_broadcast(condition);            // Wake up threads waiting for it
    g_atomic_rc_box_release_full(condition, // Decrement the writing thread's ref
                                 (GDestroyNotify) free_cond_var);

    g_mutex_unlock(&map->cache_map_mutex);
}

void
zn_cachemap_clear_zone(struct zn_cachemap *map, uint32_t zone) {
    assert(map);

    g_mutex_lock(&map->cache_map_mutex);

    for (guint i = 0; i < map->data_map[zone]->len; i++) {
        uint32_t data_id = g_array_index(map->data_map[zone], uint32_t, i);
        assert(g_hash_table_contains(map->zone_map, GUINT_TO_POINTER(data_id)));
        struct zone_map_result *res = g_hash_table_lookup(map->zone_map, GUINT_TO_POINTER(data_id));
        assert(res->type == RESULT_LOC);
        assert(res->value.location.zone == zone);

        // Erase the entry and free the zone_map_result memory
        g_hash_table_remove(map->zone_map, GINT_TO_POINTER(data_id));
        g_free(res);
    }

    g_array_remove_range(map->data_map[zone], 0, map->data_map[zone]->len);

    g_mutex_unlock(&map->cache_map_mutex);
}

void
zn_cachemap_fail(struct zn_cachemap *map, const uint32_t id) {
    g_mutex_lock(&map->cache_map_mutex);

    assert(g_hash_table_contains(map->zone_map, GINT_TO_POINTER(id)));
    struct zone_map_result *entry = g_hash_table_lookup(map->zone_map, GINT_TO_POINTER(id));
    assert(entry->type == RESULT_COND);
    g_cond_broadcast(entry->value.write_finished);            // Wake up threads waiting for it
    g_atomic_rc_box_release_full(entry->value.write_finished, // Decrement the writing thread's ref
                                 (GDestroyNotify) free_cond_var);
    g_hash_table_remove(map->zone_map,
                        GINT_TO_POINTER(id)); // Threads waiting for it must write to a new location
    g_free(entry);

    g_mutex_unlock(&map->cache_map_mutex);
}
