#ifndef ZN_MINHEAP_H
#define ZN_MINHEAP_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @struct zn_minheap_entry
 * @brief Represents an optional min-heap entry.
 */
struct zn_minheap_entry {
    bool hasdata;      /**< Indicates if the entry contains data */
    uint32_t data;     /**< Data associated with the entry */
    uint32_t priority; /**< Priority (lower value is higher priority) */
};

/**
 * @struct zn_minheap
 * @brief Min Heap structure with dynamic resizing and thread safety.
 */
struct zn_minheap {
    struct zn_minheap_entry *arr; /**< Dynamic array storing heap entries */
    uint32_t size;                /**< Current number of elements */
    uint32_t capacity;            /**< Maximum number of elements before resizing */
    GMutex mutex;                 /**< Mutex for thread-safe access */
};

/**
 * @brief Creates a new min heap.
 *
 * @param capacity Initial capacity of the heap.
 * @return Pointer to the allocated heap structure.
 */
struct zn_minheap *
zn_minheap_init(uint32_t capacity);

/**
 * @brief Destroys the heap and frees allocated memory.
 *
 * @param heap Pointer to the heap.
 */
void
zn_minheap_destroy(struct zn_minheap *heap);

/**
 * @brief Inserts a new entry into the min heap.
 *
 * @param heap Pointer to the heap.
 * @param data Data associated with the entry.
 * @param priority Priority of the entry (lower values are processed first).
 */
void
zn_minheap_insert(struct zn_minheap *heap, uint32_t data, uint32_t priority);

/**
 * @brief Extracts and returns the entry with the lowest priority.
 *
 * @param heap Pointer to the heap.
 * @return The entry with the lowest priority. If the heap is empty, `hasdata = 0`.
 */
struct zn_minheap_entry
zn_minheap_extract_min(struct zn_minheap *heap);

#endif // ZN_MINHEAP_H
