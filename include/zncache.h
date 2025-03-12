#ifndef ZNCACHE_H
#define ZNCACHE_H

#include <stdbool.h> // Needed on old C (actions, cortes)
#include <stdint.h>
#include <glib.h>

#include "cachemap.h"
#include "zone_state_manager.h"
#include "eviction_policy.h"
#include "znbackend.h"

#define MAX_OPEN_ZONES 14

/**
 * @struct zn_reader
 * @brief Manages concurrent read operations within the cache.
 *
 * The reader structure tracks query execution and workload distribution,
 * ensuring thread-safe access to cached data.
 */
struct zn_reader {
    GMutex lock;             /**< Mutex to synchronize access to the reader state. */
    uint32_t query_index;    /**< Index of the next query to be processed. */
    uint32_t workload_index; /**< Index of the workload associated with the reader. */
};

/**
 * @struct zn_cache
 * @brief Represents a cache system that manages data storage in predefined zones.
 *
 * This structure is responsible for managing zones, including tracking active,
 * free, and recently used zones. It supports parallel insertion, LRU eviction,
 * and mapping of data IDs to specific zones and offsets.
 */
struct zn_cache {
    enum zn_backend backend;      /**< SSD backend. */
    int fd;                       /**< File descriptor for associated disk. */
    uint32_t max_nr_active_zones; /**< Maximum number of zones that can be active at once. */
    uint32_t nr_zones;            /**< Total number of zones availible. */
    uint64_t max_zone_chunks;     /**< Maximum number of chunks a zone can hold. */
    size_t chunk_sz;              /**< Size of each chunk in bytes. */
    uint64_t zone_cap;            /**< Maximum storage capacity per zone in bytes. */

    struct zn_cachemap cache_map;
    struct zn_evict_policy eviction_policy;
    struct zone_state_manager zone_state;
    struct zn_reader reader; /**< Reader structure for tracking workload location. */
    gint *active_readers;    /**< Owning reference of the list of active readers per zone */
};


#endif // ZNCACHE_H
