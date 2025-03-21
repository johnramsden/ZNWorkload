#ifndef ZNBACKEND_H
#define ZNBACKEND_H

#include <stdint.h>
#include <stdbool.h>
/**
 * @enum zn_backend
 * @brief Defines SSD backends
 */
enum zn_backend {
    ZE_BACKEND_ZNS = 0,   /**< ZNS SSD backend. */
    ZE_BACKEND_BLOCK = 1, /**< Block-interface backend. */
};

/**
 * @struct zn_pair
 * @brief Represents a mapping of data to a specific zone and chunk offset.
 *
 * This structure is used to store references to locations within the cache,
 * allowing data to be efficiently retrieved or managed.
 */
struct zn_pair {
    uint32_t zone;         /**< Identifier of the zone where the data is stored. */
    uint32_t chunk_offset; /**< Offset within the zone where the data chunk is located. */
    uint32_t id;           /**< Unique ID */
    bool in_use;           /**< Defines if ze_pair is in use. */
};

#endif //ZNBACKEND_H
