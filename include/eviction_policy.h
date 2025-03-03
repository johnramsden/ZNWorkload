#pragma once

#include "eviction_policy_promotional.h"
#include "glib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @enum zn_io_type
 * @brief Defines the type of IO done
 */
enum zn_io_type {
    ZN_READ = 0,
    ZN_WRITE = 1,
};

/**
 * @enum zn_eviction_policy
 * @brief Defines eviction policies
 */
enum zn_evict_policy_type {
    ZN_EVICT_ZONE = 0,         /**< Zone granularity eviction. */
    ZN_EVICT_PROMOTE_ZONE = 1, /**< Zone granularity eviction with promotion. */
    ZN_EVICT_CHUNK = 2,        /**< Chunk granularity eviction. */
};

/** Policy specific data */
typedef void *policy_data_t;

/** A generic policy update function */
typedef void (*update_policy_t)(policy_data_t policy, uint32_t zone_id, uint32_t chunk_idx,
                                enum zn_io_type io_type);

/** A generic eviction function informed by the policy */
typedef int (*get_zone_to_evict)(policy_data_t policy);

/** A generic gc function informed by the policy. Performs garbage
    collection on behalf of the thread: the thread shouldn't have to
    do anything aside from waking up every once in a while. */
typedef void (*do_gc_t)(policy_data_t policy);

/** A generic eviction function informed by the policy. Performs
    eviction on behalf of the thread: the thread shouldn't have to do
    anything aside from waking up every once in a while. */
typedef void (*do_evict_t)(policy_data_t policy);

/** @struct zn_evict_policy
    @brief generic policy type
 */
struct zn_evict_policy {
    enum zn_evict_policy_type type; /**< Eviction policy. */
    policy_data_t data;             /**< Opaque data handle */
    update_policy_t update_policy;  /**< Called when policy needs to be updated */
    get_zone_to_evict
        get_zone_to_evict; /**< Called when eviction thread needs to evict something */
};

struct zn_chunk_evict_policy {
    enum zn_evict_policy_type type; /**< Eviction policy. */
    policy_data_t data;             /**< Opaque data handle */
    update_policy_t update_policy;  /**< Called when policy needs to be updated */
    do_evict_t do_evict;            /**< Marks chunks for eviction */
    do_gc_t do_gc;                  /**< Performs actual GC */
};

// BELOW: Should be in promotional

/** @brief Updates the promotional LRU policy
 */
void
zn_policy_promotional_update(policy_data_t policy, uint32_t zone_id, uint32_t chunk_idx,
                             enum zn_io_type io_type);

/** @brief Gets a zone to evict.
 */
int
zn_policy_promotional_get_zone_to_evict(policy_data_t policy);

/** @brief Sets up the data structure for the selected eviction policy.
 */
void
zn_evict_policy_init(struct zn_evict_policy *policy, enum zn_evict_policy_type type) {

    switch (type) {
        case ZN_EVICT_PROMOTE_ZONE: {
            struct zn_policy_promotional *data = malloc(sizeof(struct zn_policy_promotional));
            g_mutex_init(&data->policy_mutex);
            data->zone_to_lru_map = g_hash_table_new(g_direct_hash, g_direct_equal);
            g_queue_init(&data->lru_queue);

            *policy = (struct zn_evict_policy) {.type = ZN_EVICT_PROMOTE_ZONE,
                                                .data = data,
                                                .update_policy = zn_policy_promotional_update,
                                                .get_zone_to_evict =
                                                    zn_policy_promotional_get_zone_to_evict};
            break;
        }

        case ZN_EVICT_ZONE:
        case ZN_EVICT_CHUNK:
            fprintf(stderr, "NYI\n");
            exit(1);
            break;
    }
}
