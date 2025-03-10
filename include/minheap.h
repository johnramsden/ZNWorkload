#ifndef ZN_MINHEAP_H
#define ZN_MINHEAP_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @struct zn_minheap_entry
 * @brief Represents a min-heap entry.
 */
struct zn_minheap_entry {
    void * data;       /**< Data associated with the entry                */
    uint32_t priority; /**< Priority (lower value is higher priority)     */
    uint32_t index;    /**< Current array index of this entry within heap */
};

/**
 * @struct zn_minheap
 * @brief Min Heap structure with dynamic resizing and thread safety.
 *
 * Instead of an array of entries, we store an array of pointers to entries.
 * That way, a pointer to an entry remains valid even if we resize the array.
 */
struct zn_minheap {
    struct zn_minheap_entry **arr; /**< Dynamic array storing *pointers* to entries  */
    uint32_t size;                 /**< Current number of elements                   */
    uint32_t capacity;             /**< Max number of elements before resizing       */
    GMutex mutex;                  /**< Mutex for thread-safe access                 */
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
 * @param heap Pointer to the heap.
 * @param data Data associated with the entry.
 * @param priority Priority of the entry (lower values are processed first).
 * @return A pointer to the newly created zn_minheap_entry, so external code
 *         can store it if desired for later updates.
 */
struct zn_minheap_entry *
zn_minheap_insert(struct zn_minheap *heap, void * data, uint32_t priority);

/**
 * @brief Extracts and returns the entry with the lowest priority.
 *
 * @param heap Pointer to the heap.
 * @return The *pointer* to the entry with the lowest priority. If the heap
 *         is empty, returns NULL.
 *
 * @note The returned pointer is allocated by `zn_minheap_insert()`. It is your
 *       responsibility to free it, if needed, once you’re done with it.
 */
struct zn_minheap_entry *
zn_minheap_extract_min(struct zn_minheap *heap);

/**
 * @brief Updates an entry that already exists in the heap. This version updates by pointer.
 *
 * @param heap Pointer to the heap.
 * @param entry Pointer to the existing zn_minheap_entry.
 * @param new_priority New priority value.
 * @return 0 on success, -1 if the entry’s index was invalid (heap corruption or mismatch).
 */
int
zn_minheap_update_by_entry(struct zn_minheap *heap,
                           struct zn_minheap_entry *entry,
                           uint32_t new_priority);

#endif // ZN_MINHEAP_H
