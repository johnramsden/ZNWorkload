#include "eviction_policy.h"
#include "eviction_policy_chunk.h"
#include "glib.h"
#include "glibconfig.h"
#include "znutil.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <zone_state_manager.h>

void
zn_policy_chunk_update(policy_data_t _policy, struct zn_pair location,
                             enum zn_io_type io_type) {
    struct zn_policy_chunk *policy = _policy;
    assert(policy);

    g_mutex_lock(&policy->policy_mutex);
    assert(policy->chunk_to_lru_map);

    dbg_printf("State before chunk update\n");

    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &policy->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", policy->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    struct eviction_policy_chunk_zone * zpc = &policy->zone_pool[location.zone];
    struct zn_pair * zp = &zpc->chunks[location.chunk_offset];

    GList *node;
    // Should always be present (might be NULL)
    assert(g_hash_table_lookup_extended(policy->chunk_to_lru_map, zp, NULL, (gpointer *)&node));

    if (io_type == ZN_WRITE) {
        assert(!zp->in_use);
        zp->chunk_offset = location.chunk_offset;
        zp->zone = location.zone;
        zp->id = location.id;
        zp->in_use = true;
        zpc->chunks_in_use++; // Need to update here on SSD incase invalidated then re-written
        g_queue_push_tail(&policy->lru_queue, zp);
        GList *node = g_queue_peek_tail_link(&policy->lru_queue);
        g_hash_table_insert(policy->chunk_to_lru_map, zp, node);

        if (location.chunk_offset == policy->zone_max_chunks-1) {
            // We only add zones to the minheap when they are full.
            dbg_printf("Adding %p (zone=%u) to pqueue\n", (void *)zp, location.zone);
            zpc->pqueue_entry = zn_minheap_insert(policy->invalid_pqueue, zp, zpc->chunks_in_use);
            assert(zpc->pqueue_entry);
            zpc->filled = true;
        }
    } else if (io_type == ZN_READ) {
        if (node->data) {
            gpointer data = node->data;
            g_queue_delete_link(&policy->lru_queue, node);
            g_queue_push_tail(&policy->lru_queue, data);
        }

        // If node->data == NULL, the zone is not in the LRU queue. This
        // means that the zone is either not full, or has been removed
        // by the eviction thread while the read occurred. Don't do
        // anything
    }



    dbg_printf("State after chunk update\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &policy->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", policy->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    g_mutex_unlock(&policy->policy_mutex);
}

static void
zn_policy_chunk_gc(policy_data_t policy) {
    // TODO: If later separated from evict, lock here
    struct zn_policy_chunk *chunk_policy = policy;



}

int
zn_policy_chunk_evict(policy_data_t policy) {
    struct zn_policy_chunk *chunk_policy = policy;

    g_mutex_lock(&chunk_policy->policy_mutex);

    dbg_printf("State before chunk evict\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &chunk_policy->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", chunk_policy->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    uint32_t in_lru = g_queue_get_length(&chunk_policy->lru_queue);
    uint32_t free_chunks = chunk_policy->total_chunks - in_lru;

    dbg_printf("Free chunks=%u, Chunks in lru=%u, EVICT_HIGH_THRESH_CHUNKS=%u\n",
               free_chunks, in_lru, EVICT_HIGH_THRESH_CHUNKS);

    if ((in_lru == 0) || (free_chunks > EVICT_HIGH_THRESH_CHUNKS)) {
        dbg_printf("Nothing to evict\n");
        g_mutex_unlock(&chunk_policy->policy_mutex);
        return 1;
    }

    uint32_t nr_evict = EVICT_LOW_THRESH_CHUNKS-free_chunks;

    dbg_printf("Evicting %u chunks\n", nr_evict);

    // We meet thresh for eviction - evict
    for (uint32_t i = 0; i < nr_evict; i++) {
        struct zn_pair * zp = g_queue_pop_head(&chunk_policy->lru_queue);
        g_hash_table_replace(chunk_policy->chunk_to_lru_map, zp, NULL);

        // Invalidate chunk
        chunk_policy->zone_pool[zp->zone].chunks->in_use = false;
        chunk_policy->zone_pool[zp->zone].chunks_in_use--;

        // Update priority
        zn_minheap_update_by_entry(
            chunk_policy->invalid_pqueue,
            chunk_policy->zone_pool[zp->zone].pqueue_entry,
            chunk_policy->zone_pool[zp->zone].chunks_in_use
        );

        // Update ZSM, cachemap
        zsm_mark_chunk_invalid(chunk_policy->zsm, zp);
        zn_cachemap_clear_chunk(chunk_policy->cachemap, zp);

        // TODO: SSD look at invalid (not here, on write)
    }

    dbg_printf("State after chunk evict\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &chunk_policy->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", chunk_policy->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    in_lru = g_queue_get_length(&chunk_policy->lru_queue);
    free_chunks = chunk_policy->total_chunks - in_lru;
    dbg_printf("Free chunks=%u, Chunks in lru=%u, EVICT_HIGH_THRESH_CHUNKS=%u\n",
               free_chunks, in_lru, EVICT_HIGH_THRESH_CHUNKS);

    // Do GC
    zn_policy_chunk_gc(chunk_policy);

    g_mutex_unlock(&chunk_policy->policy_mutex);

    return 0;
}
