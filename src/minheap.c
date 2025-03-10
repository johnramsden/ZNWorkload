#include "minheap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Helper function to double the size of arr.
 */
static void
minheap_realloc(struct zn_minheap *heap)
{
    heap->capacity *= 2;
    // We are now reallocating an array of pointers, not entries
    heap->arr = (struct zn_minheap_entry **)realloc(
        heap->arr, sizeof(struct zn_minheap_entry *) * heap->capacity);
    assert(heap->arr);
}

/**
 * @brief Moves the entry at `index` up until the heap property is restored.
 */
static void
bubble_up(struct zn_minheap *heap, uint32_t index)
{
    while (index > 0) {
        uint32_t parent = (index - 1) / 2;
        if (heap->arr[index]->priority < heap->arr[parent]->priority) {
            // Swap the pointers
            struct zn_minheap_entry *tmp = heap->arr[index];
            heap->arr[index] = heap->arr[parent];
            heap->arr[parent] = tmp;

            // Update each entry’s `index` field
            heap->arr[index]->index = index;
            heap->arr[parent]->index = parent;

            index = parent;  // keep going upward
        } else {
            break;
        }
    }
}

/**
 * @brief Moves the entry at `index` down until the heap property is restored.
 */
static void
bubble_down(struct zn_minheap *heap, uint32_t index)
{
    while (true) {
        uint32_t left = 2 * index + 1;
        uint32_t right = 2 * index + 2;
        uint32_t smallest = index;

        if (left < heap->size &&
            heap->arr[left]->priority < heap->arr[smallest]->priority) {
            smallest = left;
        }
        if (right < heap->size &&
            heap->arr[right]->priority < heap->arr[smallest]->priority) {
            smallest = right;
        }

        if (smallest != index) {
            // Swap the pointers
            struct zn_minheap_entry *tmp = heap->arr[index];
            heap->arr[index] = heap->arr[smallest];
            heap->arr[smallest] = tmp;

            // Update each entry’s `index` field
            heap->arr[index]->index = index;
            heap->arr[smallest]->index = smallest;

            index = smallest;  // continue downward
        } else {
            break;
        }
    }
}

/**
 * @brief Initializes the heap.
 */
struct zn_minheap *
zn_minheap_init(uint32_t capacity)
{
    struct zn_minheap *heap = (struct zn_minheap *)malloc(sizeof(struct zn_minheap));
    if (!heap) {
        return NULL;
    }

    heap->arr = (struct zn_minheap_entry **)malloc(sizeof(struct zn_minheap_entry *) * capacity);
    if (!heap->arr) {
        free(heap);
        return NULL;
    }

    heap->size = 0;
    heap->capacity = capacity;
    g_mutex_init(&heap->mutex);
    return heap;
}

/**
 * @brief Destroys the heap.
 */
void
zn_minheap_destroy(struct zn_minheap *heap)
{
    g_mutex_lock(&heap->mutex);

    for (uint32_t i = 0; i < heap->size; i++) {
       free(heap->arr[i]);
    }

    free(heap->arr);

    g_mutex_unlock(&heap->mutex);
    g_mutex_clear(&heap->mutex);

    free(heap);
}

struct zn_minheap_entry *
zn_minheap_insert(struct zn_minheap *heap, void * data, uint32_t priority)
{
    g_mutex_lock(&heap->mutex);

    if (heap->size == heap->capacity) {
        minheap_realloc(heap);
    }

    struct zn_minheap_entry *new_entry =
        (struct zn_minheap_entry *)malloc(sizeof(struct zn_minheap_entry));
    assert(new_entry);

    new_entry->data = data;
    new_entry->priority = priority;
    new_entry->index = heap->size;

    // Add pointer to new entry at the end of the array
    heap->arr[heap->size] = new_entry;
    heap->size++;

    bubble_up(heap, new_entry->index);

    g_mutex_unlock(&heap->mutex);

    return new_entry;
}

struct zn_minheap_entry *
zn_minheap_extract_min(struct zn_minheap *heap)
{
    g_mutex_lock(&heap->mutex);

    if (heap->size == 0) {
        g_mutex_unlock(&heap->mutex);
        return NULL; // Heap empty
    }

    // The root pointer (lowest priority)
    struct zn_minheap_entry *min_entry = heap->arr[0];

    // Move the last pointer into the root position
    heap->size--;
    if (heap->size > 0) {
        heap->arr[0] = heap->arr[heap->size];
        heap->arr[0]->index = 0; // its new position
        bubble_down(heap, 0);
    }

    g_mutex_unlock(&heap->mutex);

    // Caller now owns this pointer. They can free it or keep it.
    return min_entry;
}

/**
 * @brief Updates an existing entry’s priority, by pointer.
 */
int
zn_minheap_update_by_entry(struct zn_minheap *heap,
                           struct zn_minheap_entry *entry,
                           uint32_t new_priority) {
    g_mutex_lock(&heap->mutex);

    if (!entry || entry->index >= heap->size) {
        g_mutex_unlock(&heap->mutex);
        return -1; // invalid entry
    }

    uint32_t old_priority = entry->priority;
    entry->priority = new_priority;

    // Priority decreased, bubble up
    if (new_priority < old_priority) {
        bubble_up(heap, entry->index);
    } else if (new_priority > old_priority) {
        // Priority increased
        bubble_down(heap, entry->index);
    }

    g_mutex_unlock(&heap->mutex);
    return 0;
}
