#include "eviction_policy.h"

#include "eviction_policy_promotional.h"

#include <assert.h>
#include <glib.h>
#include <stdio.h>

void
zn_evict_policy_init(struct zn_evict_policy *policy, enum zn_evict_policy_type type,
                     uint32_t zone_max_chunks) {

    switch (type) {
        case ZN_EVICT_PROMOTE_ZONE: {
            struct zn_policy_promotional *data = malloc(sizeof(struct zn_policy_promotional));
            g_mutex_init(&data->policy_mutex);
            data->zone_to_lru_map = g_hash_table_new(g_direct_hash, g_direct_equal);

            data->zone_max_chunks = zone_max_chunks;

            assert(data->zone_to_lru_map);
            g_queue_init(&data->lru_queue);

            *policy = (struct zn_evict_policy) {
                .type = ZN_EVICT_PROMOTE_ZONE,
                .data = data,
                .update_policy = zn_policy_promotional_update,
                .get_zone_to_evict = zn_policy_promotional_get_zone_to_evict,
            };
            break;
        }

        case ZN_EVICT_ZONE:
        case ZN_EVICT_CHUNK:
            fprintf(stderr, "NYI\n");
            exit(1);
            break;
    }
}
