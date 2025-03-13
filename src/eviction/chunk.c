#include "eviction_policy.h"
#include "eviction_policy_chunk.h"
#include "znutil.h"
#include "minheap.h"
#include "zone_state_manager.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <glib.h>
#include <glibconfig.h>

void
zn_policy_chunk_update(policy_data_t _policy, struct zn_pair location,
                             enum zn_io_type io_type) {
    struct zn_policy_chunk *p = _policy;
    assert(p);

    g_mutex_lock(&p->policy_mutex);
    assert(p->chunk_to_lru_map);

    dbg_printf("State before chunk update\n");

    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &p->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", p->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    struct eviction_policy_chunk_zone * zpc = &p->zone_pool[location.zone];
    struct zn_pair * zp = &zpc->chunks[location.chunk_offset];

    GList *node;
    // Should always be present (might be NULL)
    assert(g_hash_table_lookup_extended(p->chunk_to_lru_map, zp, NULL, (gpointer *)&node));

    if (io_type == ZN_WRITE) {
        assert(!zp->in_use);
        zp->chunk_offset = location.chunk_offset;
        zp->zone = location.zone;
        zp->id = location.id;
        zp->in_use = true;
        zpc->chunks_in_use++; // Need to update here on SSD incase invalidated then re-written
        zpc->zone_id = location.zone;
        g_queue_push_tail(&p->lru_queue, zp);
        GList *node = g_queue_peek_tail_link(&p->lru_queue);
        g_hash_table_insert(p->chunk_to_lru_map, zp, node);

        if (location.chunk_offset == p->cache->max_zone_chunks-1) {
            // We only add zones to the minheap when they are full.
            dbg_printf("Adding %p (zone=%u) to pqueue\n", (void *)zp, location.zone);
            zpc->pqueue_entry = zn_minheap_insert(p->invalid_pqueue, zpc, zpc->chunks_in_use);
            assert(zpc->pqueue_entry);
            zpc->filled = true;
        }
    } else if (io_type == ZN_READ) {
        if (node->data) {
            gpointer data = node->data;
            g_queue_delete_link(&p->lru_queue, node);
            g_queue_push_tail(&p->lru_queue, data);
        }

        // If node->data == NULL, the zone is not in the LRU queue. This
        // means that the zone is either not full, or has been removed
        // by the eviction thread while the read occurred. Don't do
        // anything
    }



    dbg_printf("State after chunk update\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &p->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", p->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    g_mutex_unlock(&p->policy_mutex);
}

static void
zn_policy_chunk_gc(policy_data_t policy) {
    // TODO: If later separated from evict, lock here
    struct zn_policy_chunk *p = policy;

    uint32_t free_zones = zsm_get_num_free_zones(&p->cache->zone_state);
    if (free_zones > EVICT_HIGH_THRESH_ZONES) {
        return;
    }

    while (free_zones < EVICT_LOW_THRESH_ZONES) {
        struct zn_minheap_entry *ent = zn_minheap_extract_min(p->invalid_pqueue);
        struct eviction_policy_chunk_zone * zone = ent->data;
        assert(zone);
        dbg_printf("Found minheap_entry priority=%u, chunks_in_use=%u, zone=%u\n",
            ent->priority,  zone->chunks_in_use, zone->zone_id);
        dbg_printf("zone[%u] chunks:\n", zone->zone_id);
        dbg_print_zn_pair_list(zone->chunks, p->cache->max_zone_chunks);



        // Naive?
        for (uint32_t i = 0; i < p->cache->max_zone_chunks; i++) {
            if (!zone->chunks[i].in_use) {
                continue;
            }
            // TODO
            assert(!"NYI");

            // read in old data
            // grab active zone
            // write out old data
            // Update mappings

#ifdef WTF
            // TODO: Need a lock? Not using zn_cachemap_find
            // TODO: Reader count?

            // TODO: FAIL?
            // Repeatedly attempt to get an active zone. This function can fail when there all active
            // zones are writing, so put this into a while loop.
            struct zn_pair location;
            while (true) {
                enum zsm_get_active_zone_error ret = zsm_get_active_zone(&p->cache->zone_state, &location);

                if (ret == ZSM_GET_ACTIVE_ZONE_RETRY) {
                    g_thread_yield();
                } else if (ret == ZSM_GET_ACTIVE_ZONE_ERROR) {
                    assert(!"???");
                } else if (ret == ZSM_GET_ACTIVE_ZONE_EVICT) {
                    assert(!"???");
                } else {
                    break;
                }
            }

            unsigned char *data = zn_read_from_disk(p->cache, &zone->chunks[i]);
            int ret = zn_write_out(
                p->cache->fd,
                p->cache->chunk_sz,
                data,
                WRITE_GRANULARITY,

            );

            // Write buffer to disk, 4kb blocks at a time
            unsigned long long wp =
                CHUNK_POINTER(cache->zone_cap, cache->chunk_sz, location.chunk_offset, location.zone);
            if (zn_write_out(cache->fd, cache->chunk_sz, data, WRITE_GRANULARITY, wp) != 0) {
                dbg_printf("Couldn't write to fd at wp=%llu\n", wp);
                goto UNDO_ZONE_GET;
            }

            // Update metadata
            zn_cachemap_insert(&p->cache->cache_map, id, location);

            zsm_return_active_zone(&p->cache->zone_state, &location);
#endif


        }
        free_zones = zsm_get_num_free_zones(&p->cache->zone_state);
    }
}

int
zn_policy_chunk_evict(policy_data_t policy) {
    struct zn_policy_chunk *p = policy;

    g_mutex_lock(&p->policy_mutex);

    dbg_printf("State before chunk evict\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &p->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", p->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    uint32_t in_lru = g_queue_get_length(&p->lru_queue);
    uint32_t free_chunks = p->total_chunks - in_lru;

    dbg_printf("Free chunks=%u, Chunks in lru=%u, EVICT_HIGH_THRESH_CHUNKS=%u\n",
               free_chunks, in_lru, EVICT_HIGH_THRESH_CHUNKS);

    if ((in_lru == 0) || (free_chunks > EVICT_HIGH_THRESH_CHUNKS)) {
        dbg_printf("Nothing to evict\n");
        g_mutex_unlock(&p->policy_mutex);
        return 1;
    }

    uint32_t nr_evict = EVICT_LOW_THRESH_CHUNKS-free_chunks;

    dbg_printf("Evicting %u chunks\n", nr_evict);

    // We meet thresh for eviction - evict
    for (uint32_t i = 0; i < nr_evict; i++) {
        struct zn_pair * zp = g_queue_pop_head(&p->lru_queue);
        g_hash_table_replace(p->chunk_to_lru_map, zp, NULL);

        // Invalidate chunk
        p->zone_pool[zp->zone].chunks[zp->chunk_offset].in_use = false;
        p->zone_pool[zp->zone].chunks_in_use--;

        // Update priority
        zn_minheap_update_by_entry(
            p->invalid_pqueue,
            p->zone_pool[zp->zone].pqueue_entry,
            p->zone_pool[zp->zone].chunks_in_use
        );

        // Update ZSM, cachemap
        zsm_mark_chunk_invalid(&p->cache->zone_state, zp);
        zn_cachemap_clear_chunk(&p->cache->cache_map, zp);

        // TODO: SSD look at invalid (not here, on write)
    }

    dbg_printf("State after chunk evict\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &p->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", p->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    in_lru = g_queue_get_length(&p->lru_queue);
    free_chunks = p->total_chunks - in_lru;
    dbg_printf("Free chunks=%u, Chunks in lru=%u, EVICT_HIGH_THRESH_CHUNKS=%u\n",
               free_chunks, in_lru, EVICT_HIGH_THRESH_CHUNKS);

    // Do GC
    zn_policy_chunk_gc(p);

    g_mutex_unlock(&p->policy_mutex);

    return 0;
}
