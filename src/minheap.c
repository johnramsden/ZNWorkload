#include "minheap.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct zn_minheap *
zn_minheap_init(uint32_t capacity) {
    struct zn_minheap *heap = (struct zn_minheap *) malloc(sizeof(struct zn_minheap));
    if (heap == NULL) {
        return NULL;
    }
    heap->arr = (struct zn_minheap_entry *) malloc(sizeof(struct zn_minheap_entry) * capacity);
    heap->size = 0;
    heap->capacity = capacity;
    g_mutex_init(&heap->mutex);
    return heap;
}

void
zn_minheap_destroy(struct zn_minheap *heap) {
    g_mutex_lock(&heap->mutex);
    free(heap->arr);
    g_mutex_unlock(&heap->mutex);
    g_mutex_clear(&heap->mutex);
    free(heap);
}

static void
minheap_realloc(struct zn_minheap *heap) {
    heap->capacity *= 2;
    heap->arr = (struct zn_minheap_entry *) realloc(
        heap->arr, sizeof(struct zn_minheap_entry) * heap->capacity);
    assert(heap->arr);
}

void
zn_minheap_insert(struct zn_minheap *heap, uint32_t data, uint32_t priority) {
    g_mutex_lock(&heap->mutex);

    if (heap->size == heap->capacity) {
        minheap_realloc(heap);
    }

    // Add data to end of array
    heap->arr[heap->size].data = data;
    heap->arr[heap->size].priority = priority;
    heap->arr[heap->size].hasdata = true;

    // We are now out of order, swap child with parent until ordered

    uint32_t index = heap->size;
    while (index > 0) {
        uint32_t parent = (index - 1) / 2;
        if (heap->arr[index].priority < heap->arr[parent].priority) {
            struct zn_minheap_entry temp = heap->arr[index];
            heap->arr[index] = heap->arr[parent];
            heap->arr[parent] = temp;
            index = parent;
        } else {
            break;
        }
    }

    heap->size++;
    g_mutex_unlock(&heap->mutex);
}

struct zn_minheap_entry
zn_minheap_extract_min(struct zn_minheap *heap) {
    g_mutex_lock(&heap->mutex);

    if (heap->size == 0) {
        g_mutex_unlock(&heap->mutex);
        return (struct zn_minheap_entry) {.hasdata = false}; // Heap is empty
    }

    // Grab head, to be removed
    struct zn_minheap_entry min_entry = heap->arr[0];

    // Make new head the last entry
    heap->arr[0] = heap->arr[--heap->size];

    // Re-order, move head down until ordered
    uint32_t index = 0;
    while (true) {
        uint32_t smallest = index;  // Current location
        uint32_t left = 2 * index + 1;   // Left child
        uint32_t right = 2 * index + 2;  // Right child

        if (left < heap->size && heap->arr[left].priority < heap->arr[smallest].priority) {
            smallest = left;
        }
        if (right < heap->size && heap->arr[right].priority < heap->arr[smallest].priority) {
            smallest = right;
        }

        if (smallest != index) {
            // Swap, wrong order
            struct zn_minheap_entry temp = heap->arr[index];
            heap->arr[index] = heap->arr[smallest];
            heap->arr[smallest] = temp;
            index = smallest;
        } else {
            // No children, or no match
            break;
        }
    }

    g_mutex_unlock(&heap->mutex);
    min_entry.hasdata = true;
    return min_entry;
}
