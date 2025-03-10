#include "eviction_policy.h"
#include "eviction_policy_chunk.h"
#include "glib.h"
#include "glibconfig.h"
#include "znutil.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

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
            zn_minheap_insert(policy->invalid_pqueue, zp, zpc->chunks_in_use);
        }
    } else if (io_type == ZN_READ) {
        if (node->data) {
            gpointer data = node->data;
            g_queue_delete_link(&policy->lru_queue, node);
            g_queue_push_tail(&policy->lru_queue, data);
        }

        // TODO: I THINK: If node->data == NULL, the zone is not in the LRU queue. This
        // means that the zone is either not full, or has been removed
        // by the eviction thread while the read occurred. Don't do
        // anything
    }



    dbg_printf("State after chunk update\n");
    dbg_print_g_queue("lru_queue (zone,chunk,id,in_use)", &policy->lru_queue, PRINT_G_QUEUE_ZN_PAIR);
    dbg_print_g_hash_table("chunk_to_lru_map (id,zone,chunk,in_use)", policy->chunk_to_lru_map, PRINT_G_HASH_TABLE_ZN_PAIR_NODE);

    g_mutex_unlock(&policy->policy_mutex);
}

int
zn_policy_chunk_get_chunk_to_evict(policy_data_t policy) {
    (void)policy;
    return 0;
}
