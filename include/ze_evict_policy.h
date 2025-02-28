#pragma once

#include "glib.h"
#include "ze_promotional_eviction_policy.h"
#include <stdio.h>
#include <stdlib.h>
enum ze_io_type {
    ZE_READ = 0,
    ZE_WRITE = 1,
};


/**
 * @enum ze_eviction_policy
 * @brief Defines eviction policies
 */
enum ze_evict_policy_type {
    ZE_EVICT_ZONE = 0,  /**< Zone granularity eviction. */
    ZE_EVICT_PROMOTE_ZONE = 1,  /**< Zone granularity eviction with promotion. */
    ZE_EVICT_CHUNK = 2, /**< Chunk granularity eviction. */
};



typedef void* policy_data_t;
typedef void(*update_policy_t)(policy_data_t policy, int zone_id, int chunk_idx, enum ze_io_type io_type);
typedef int(*get_zone_to_evict)(policy_data_t policy);
typedef void(*do_gc_t)(policy_data_t policy);
typedef void(*do_evict_t)(policy_data_t policy);

struct ze_evict_policy {
    enum ze_evict_policy_type type; /**< Eviction policy. */
	policy_data_t		data;	/**< Opaque data handle */
    update_policy_t		update_policy;	/**< Called when policy needs to be updated */
    get_zone_to_evict	get_zone_to_evict;	/**< Called when eviction thread needs to evict something */
};

struct ze_chunk_evict_policy {
    enum ze_evict_policy_type type; /**< Eviction policy. */
    policy_data_t	data;		/**< Opaque data handle */
    update_policy_t update_policy;	/**< Called when policy needs to be updated */
    do_evict_t		do_evict;	/**< Marks chunks for eviction */    
	do_gc_t			do_gc;		/**< Performs actual GC */
};

void
promote_update_policy(policy_data_t policy, int zone_id, int chunk_idx, enum ze_io_type io_type);

int
promote_get_zone_to_evict(policy_data_t policy);

struct ze_evict_policy
make_policy(enum ze_evict_policy_type type) {

    switch (type) {
    case ZE_EVICT_PROMOTE_ZONE: {
        struct ze_promotional_policy *data = malloc(sizeof(struct ze_promotional_policy));
        g_mutex_init(&data->policy_mutex);
        data->zone_to_lru_map = g_hash_table_new(g_direct_hash, g_direct_equal);
        g_queue_init(&data->lru_queue);

		return (struct ze_evict_policy) {
			.type = ZE_EVICT_PROMOTE_ZONE,
			.data = data,
			.update_policy = promote_update_policy,
			.get_zone_to_evict = promote_get_zone_to_evict
		};
    }

    case ZE_EVICT_ZONE:
    case ZE_EVICT_CHUNK:
        fprintf(stderr, "NYI\n");
        exit(1);
        break;
    }
}
