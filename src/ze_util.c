#include "ze_cache.h"
#include "ze_util.h"

#include <stdio.h>

void
print_g_hash_table(char *name, GHashTable *hash_table) {
    GHashTableIter iter;
    gpointer key, value;

    printf("hash table %s:\n", name);

    g_hash_table_iter_init(&iter, hash_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        struct ze_pair *zp = (struct ze_pair *) value;
        printf("\tKey: %d, Value: zone=%u, chunk=%u, id=%u, in_use=%s\n", GPOINTER_TO_INT(key), zp->zone,
               zp->chunk_offset, zp->id, zp->in_use ? "true" : "false");
    }
}

void
print_g_queue(char *name, GQueue *queue) {
    printf("Printing queue %s: ", name);
    for (GList *node = queue->head; node != NULL; node = node->next) {
        printf("%d ", GPOINTER_TO_INT(node->data));
    }
    puts("");
}

unsigned char *
generate_random_buffer(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // Allocate memory for the buffer
    unsigned char *buffer = (unsigned char *) malloc(size);
    if (buffer == NULL) {
        return NULL;
    }

    srand(SEED);

    for (size_t i = 0; i < size; i++) {
        buffer[i] = (unsigned char) (rand() % 256); // Random byte (0-255)
    }

    return buffer;
}

void
nomem() {
    fprintf(stderr, "ERROR: No memory\n");
    exit(ENOMEM);
}

void
print_zbd_info(struct zbd_info *info) {
    printf("vendor_id=%s\n", info->vendor_id);
    printf("nr_sectors=%llu\n", info->nr_sectors);
    printf("nr_lblocks=%llu\n", info->nr_lblocks);
    printf("nr_pblocks=%llu\n", info->nr_pblocks);
    printf("zone_size (bytes)=%llu\n", info->zone_size);
    printf("zone_sectors=%u\n", info->zone_sectors);
    printf("lblock_size=%u\n", info->lblock_size);
    printf("pblock_size=%u\n", info->pblock_size);
    printf("nr_zones=%u\n", info->nr_zones);
    printf("max_nr_open_zones=%u\n", info->max_nr_open_zones);
    printf("max_nr_active_zones=%u\n", info->max_nr_active_zones);
    printf("model=%u\n", info->model);
}