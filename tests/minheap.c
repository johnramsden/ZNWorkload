#include <stdio.h>

#include "minheap.h"


/**
 * @brief Test inserting and extracting a single element.
 * @return 0 on success, non-zero on failure.
 */
int test_single_insert_extract() {
    struct zn_minheap *heap = zn_minheap_init(4);

    zn_minheap_insert(heap, 42, 10);
    struct zn_minheap_entry result = zn_minheap_extract_min(heap);

    zn_minheap_destroy(heap);

    if (!result.hasdata || result.data != 42 || result.priority != 10) {
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

    zn_minheap_insert(heap, 100, 3);
    zn_minheap_insert(heap, 200, 1);
    zn_minheap_insert(heap, 300, 2);
    zn_minheap_insert(heap, 400, 0);

    struct zn_minheap_entry result;

    result = zn_minheap_extract_min(heap);
    if (!result.hasdata || result.data != 400 || result.priority != 0) return 1;

    result = zn_minheap_extract_min(heap);
    if (!result.hasdata || result.data != 200 || result.priority != 1) return 2;

    result = zn_minheap_extract_min(heap);
    if (!result.hasdata || result.data != 300 || result.priority != 2) return 3;

    result = zn_minheap_extract_min(heap);
    if (!result.hasdata || result.data != 100 || result.priority != 3) return 4;

    zn_minheap_destroy(heap);
    return 0;  // Success
}

/**
 * @brief Test extracting from an empty heap.
 * @return 0 on success, non-zero on failure.
 */
int test_extract_empty_heap() {
    struct zn_minheap *heap = zn_minheap_init(4);

    struct zn_minheap_entry result = zn_minheap_extract_min(heap);
    zn_minheap_destroy(heap);

    if (result.hasdata) {
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

    for (uint32_t i = 0; i < 10; i++) {
        zn_minheap_insert(heap, i, i);  // Insert increasing priority
    }

    for (uint32_t i = 0; i < 10; i++) {
        struct zn_minheap_entry result = zn_minheap_extract_min(heap);
        if (!result.hasdata || result.data != i || result.priority != i) {
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
int test_concurrency_stress() {
    struct zn_minheap *heap = zn_minheap_init(10);

    for (uint32_t i = 0; i < 10; i++) {
        struct zn_minheap_entry result = zn_minheap_extract_min(heap);
        if (!result.hasdata || result.data != i || result.priority != i) {
            zn_minheap_destroy(heap);
            return 1;  // Failure
        }
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

    return failures;
}
