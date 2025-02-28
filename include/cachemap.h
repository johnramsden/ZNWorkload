#ifndef CACHEMAP_H
#define CACHEMAP_H

#include "global.h"

#include <glib.h>
#include <stdint.h>

struct cachemap {
    GHashTable *zone_id_to_data_id; /**< Maps zone IDs to data IDs. */
    GHashTable *data_id_to_zone;    /**< Maps data IDs to zones. */
};

/**
 * @enum cachemap_get_status
 * @brief An enumeration describing what the caller needs to do next.
 */
enum cachemap_get_status {
    CACHEMAP_GET_FOUND,       /**< The data was found, zone_id/chunk are valid. */
    CACHEMAP_GET_NEEDS_WRITE, /**< The data wasn't found; caller should write. */
    CACHEMAP_GET_NEEDS_WAIT   /**< The data wasn't found; caller should wait. */
};

/**
 * @struct cachemap_get_result
 * @brief A result structure for gets with all relevant return fields.
 */
struct cachemap_get_result {
    enum cachemap_get_status status; /**< The status of this lookup. */
    struct zone_pair zone_pair;      /**< The zone and chunk where the data is stored. */
};

/**
 * Initialize the cachemap
 *
 * @param cm Cahemap to initialize
 */
void
cachemap_init(struct cachemap *cm);

/**
 * Destroy the cachemap
 *
 * @param cm Cahemap to destroy
 */
void
cachemap_destroy(struct cachemap *cm);

/**
 * cachemap_get_result:
 * @data_id: The ID of the data element to find or prepare.
 *
 * Returns a cache_result_t structure. On success (CACHE_DATA_FOUND),
 * zone_id and chunk_ptr are valid. If the data does not exist, the
 * returned status will be CACHE_DATA_NEEDS_WRITE or CACHE_DATA_NEEDS_WAIT,
 * and cond_var/message may instruct the caller to wait or proceed with a write.
 * If is_read_request is true, this function will also handle
 * incrementing the active-reader count internally.
 */
struct cachemap_get_result
cachemap_get(struct cachemap *cm, uint32_t data_id);

/**
 * cachemap_insert

 * @cm: Pointer to the cachemap structure.
 * @data_id: The ID of the data element to insert.
 * @zone_pair: Structure containing the zone ID and chunk pointer where
 *             the data has been written.
 *
 * @brief Inserts a new mapping into the data structure, called by the thread
 * once it has finished writing to the zone. This function also updates
 * the Zone ID to Data ID map.
 */
void
cachemap_insert(struct cachemap *cm, uint32_t data_id, struct zone_pair zone_pair);

/**
 * cachemap_clear_zone:
 * @cm: Pointer to the cachemap structure.
 * @zone_id: The ID of the zone to clear.
 *
 * @brief Clears all entries of a specified zone in the mapping, typically called
 * by eviction threads. This function also removes the corresponding
 * mappings in the Zone ID to Data ID map.
 */
void
cachemap_clear_zone(struct cachemap *cm, uint32_t zone_id);

#endif // CACHEMAP_H
