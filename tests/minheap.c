#include <stdio.h>

#include "minheap.h"


/**
 * @brief Test inserting and extracting a single element.
 * @return 0 on success, non-zero on failure.
 */
int test_single_insert_extract() {
    struct zn_minheap *heap = zn_minheap_init(4);

    int d = 42;

    zn_minheap_insert(heap, &d, 10);
    struct zn_minheap_entry *result = zn_minheap_extract_min(heap);

    zn_minheap_destroy(heap);

    if (!result || *((int *)result->data) != 42 || result->priority != 10) {
        return 1;  // Failure
    }
    return 0;  // Success
}

/**
 * @brief Test inserting multiple elements and extracting in priority order.
 * @return 0 on success, non-zero on failure.
 */
int test_multiple_insert_extract() {
    struct zn_minheap *heap = zn_minheap_init(4);

    uint32_t entries = 4;

    int *d = malloc(sizeof(int) * entries);

    for (int i = 0; i < entries; i++) {
        d[i] = (i+1)*100; // 100, 200, 300, 400
    }

    zn_minheap_insert(heap, &d[0], 3);
    zn_minheap_insert(heap, &d[1], 1);
    zn_minheap_insert(heap, &d[2], 2);
    zn_minheap_insert(heap, &d[3], 0);

    struct zn_minheap_entry * result;

    result = zn_minheap_extract_min(heap);
    if (!result || *((int *)result->data) != 400 || result->priority != 0) return 1;

    result = zn_minheap_extract_min(heap);
    if (!result || *((int *)result->data) != 200 || result->priority != 1) return 2;

    result = zn_minheap_extract_min(heap);
    if (!result || *((int *)result->data) != 300 || result->priority != 2) return 3;

    result = zn_minheap_extract_min(heap);
    if (!result || *((int *)result->data) != 100 || result->priority != 3) return 4;

    zn_minheap_destroy(heap);
    return 0;  // Success
}

/**
 * @brief Test extracting from an empty heap.
 * @return 0 on success, non-zero on failure.
 */
int test_extract_empty_heap() {
    struct zn_minheap *heap = zn_minheap_init(4);

    struct zn_minheap_entry *result = zn_minheap_extract_min(heap);
    zn_minheap_destroy(heap);

    if (result != NULL) {
        return 1;  // Failure (should be empty)
    }
    return 0;  // Success
}

/**
 * @brief Test heap expansion when inserting more elements than initial capacity.
 * @return 0 on success, non-zero on failure.
 */
int test_heap_expansion() {
    struct zn_minheap *heap = zn_minheap_init(2); // Small initial size

    uint32_t entries = 10;

    int *d = malloc(sizeof(int) * entries);

    for (int i = 0; i < entries; i++) {
        d[i] = i;
    }

    for (uint32_t i = 0; i < 10; i++) {
        zn_minheap_insert(heap, &d[i], i);  // Insert increasing priority
    }

    for (uint32_t i = 0; i < 10; i++) {
        struct zn_minheap_entry *result = zn_minheap_extract_min(heap);
        if (!result || *((int *)result->data) != i || result->priority != i) {
            zn_minheap_destroy(heap);
            return 1;  // Failure
        }
    }

    zn_minheap_destroy(heap);
    return 0;  // Success
}
/**
 * @brief Stress test with multiple threads
 * @return 0 on success, non-zero on failure.
 */
int test_update() {
    struct zn_minheap *heap = zn_minheap_init(10);

    uint32_t entries = 5;

    struct zn_minheap_entry **results = malloc(sizeof(struct zn_minheap_entry *) * entries);
    int *d = malloc(sizeof(int) * entries);

    for (uint32_t i = 0; i < entries; i++) {
        d[i] = i;
        results[i] = zn_minheap_insert(heap, &d[i], i+1);
    }

    zn_minheap_update_by_entry(heap, results[2], 0);

    struct zn_minheap_entry *e = zn_minheap_extract_min(heap);

    if (e != results[2]) {
        return 1;
    }
    if (*((int *)e->data) != 2 || e->priority != 0) {
        return 1;
    }

    e = zn_minheap_extract_min(heap);
    if (*((int *)e->data) != 0 || e->priority != 1 || e != results[0]) {
        return 1;
    }
    e = zn_minheap_extract_min(heap);
    if (*((int *)e->data) != 1 || e->priority != 2 || e != results[1]) {
        return 1;
    }
    e = zn_minheap_extract_min(heap);
    if (*((int *)e->data) != 3 || e->priority != 4 || e != results[3]) {
        return 1;
    }
    e = zn_minheap_extract_min(heap);
    if (*((int *)e->data) != 4 || e->priority != 5 || e != results[4]) {
        return 1;
    }

    zn_minheap_destroy(heap);
    return 0;  // Success
}

/**
 * @brief Runs all test cases and prints the results.
 */
int main() {
    int failures = 0;

    if (test_single_insert_extract() != 0) {
        printf("Test FAILED: test_single_insert_extract()\n");
        failures++;
    } else {
        printf("Test PASSED: test_single_insert_extract()\n");
    }

    if (test_multiple_insert_extract() != 0) {
        printf("Test FAILED: test_multiple_insert_extract()\n");
        failures++;
    } else {
        printf("Test PASSED: test_multiple_insert_extract()\n");
    }

    if (test_extract_empty_heap() != 0) {
        printf("Test FAILED: test_extract_empty_heap()\n");
        failures++;
    } else {
        printf("Test PASSED: test_extract_empty_heap()\n");
    }

    if (test_heap_expansion() != 0) {
        printf("Test FAILED: test_heap_expansion()\n");
        failures++;
    } else {
        printf("Test PASSED: test_heap_expansion()\n");
    }

    if (test_update() != 0) {
        printf("Test FAILED: test_update()\n");
        failures++;
    } else {
        printf("Test PASSED: test_update()\n");
    }

    return failures;
}
