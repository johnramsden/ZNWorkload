#ifndef ZONE_EVICT_IDEAL_H
#define ZONE_EVICT_IDEAL_H

#include <stdint.h>

/**
 * @struct ze_pair
 * @brief Represents a mapping of data to a specific zone and chunk offset.
 *
 * This structure is used to store references to locations within the cache,
 * allowing data to be efficiently retrieved or managed.
 */
struct ze_pair {
    uint32_t zone;         /**< Identifier of the zone where the data is stored. */
    uint32_t chunk_offset; /**< Offset within the zone where the data chunk is located. */
    uint32_t id;           /**< Unique ID */
    bool in_use;           /**< Defines if ze_pair is in use. */
};

#endif // ZONE_EVICT_IDEAL_H
