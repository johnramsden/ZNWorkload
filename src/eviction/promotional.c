#include "glib.h"
#include "glibconfig.h"
#include "eviction_policy.h"
#include "znutil.h"
#include "eviction_policy_promotional.h"
#include <stdint.h>
#include <stdlib.h>

void
promote_update_policy(policy_data_t _policy, uint32_t zone_id,
                      uint32_t chunk_idx, enum ze_io_type io_type) {
	struct ze_promotional_policy* policy = _policy;
	g_mutex_lock(&policy->policy_mutex);

    // We only add zones to the LRU when they are full.
    if (io_type == ZE_WRITE && chunk_idx == policy->num_zones) {
        g_queue_push_tail(&policy->lru_queue, GUINT_TO_POINTER(zone_id));
        GList *node = g_queue_peek_tail_link(&policy->lru_queue);
        g_hash_table_insert(policy->zone_to_lru_map, GUINT_TO_POINTER(zone_id), node);

    } else if (io_type == ZE_READ) {
        GList* node = g_hash_table_lookup(policy->zone_to_lru_map,
                                               GUINT_TO_POINTER(zone_id));
        if (node) {
			gpointer data = node->data;
            g_queue_delete_link(&policy->lru_queue, node);
            g_queue_push_tail(&policy->lru_queue, data);
        }

        // If lru_loc == NULL, the zone is not in the LRU queue. This
        // means that the zone is either not full, or has been removed
        // by the eviction thread while the read occurred. Don't do
        // anything
    }

	g_mutex_unlock(&policy->policy_mutex);
    return;
}

int
promote_get_zone_to_evict(policy_data_t policy) {
	struct ze_promotional_policy* promote_policy = policy;

	g_mutex_lock(&promote_policy->policy_mutex);

    dbg_print_g_queue("lru_queue", &promote_policy->lru_queue);

    // Remove from LRU and hash map
    uint32_t zone_id = GPOINTER_TO_UINT(g_queue_pop_head(&promote_policy->lru_queue));
    g_hash_table_replace(promote_policy->zone_to_lru_map,
                         GUINT_TO_POINTER(zone_id), NULL);
    dbg_printf("Evicted zone=%u\n", zone_id);

	g_mutex_unlock(&promote_policy->policy_mutex);
    return zone_id;
}
