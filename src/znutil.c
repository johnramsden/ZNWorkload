#include "znutil.h"
#include "zncache.h"
#include "zone_state_manager.h"

#include <stdio.h>
#include <assert.h>

inline static void
print_g_hash_table_zn_pair(gpointer key, gpointer value) {
    struct zn_pair *zp = (struct zn_pair *) value;
    if (zp) {
        printf("[%d: (zone=%u, chunk=%u, id=%u, in_use=%s)], ", GPOINTER_TO_INT(key),
               zp->zone, zp->chunk_offset, zp->id, zp->in_use ? "true" : "false");
    } else {
        printf("[%d: (NULL)], ", GPOINTER_TO_INT(key));
    }
}

inline static void
print_g_hash_table_prom_lru_node(gpointer key, gpointer value) {
    GList *node = (GList *) value;
    if (node) {
		printf("[%d: %u], ", GPOINTER_TO_INT(key), GPOINTER_TO_INT(node->data));
    } else {
		printf("[%d: %s], ", GPOINTER_TO_INT(key), "NULL");
    }
}

inline static void
print_g_hash_table_zn_pair_node(gpointer key, gpointer value) {
    GList *node = (GList *) value;
    if (node) {
        struct zn_pair *zp = node->data;
		printf("[%p: (zone=%u, chunk=%u, id=%u, in_use=%s)], ",
		       key, zp->zone, zp->chunk_offset, zp->id, zp->in_use ? "true" : "false");
    } else {
		printf("[%p: (%s)], ", key, "NULL");
    }
}

void
print_g_hash_table(char *name, GHashTable *hash_table, enum print_g_hash_table_type type) {
    GHashTableIter iter;
    gpointer key, value;

    printf("hash table %s: ", name);

    g_hash_table_iter_init(&iter, hash_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        switch (type) {
            case PRINT_G_HASH_TABLE_GINT:
                print_g_hash_table_zn_pair(key, value); break;
            case PRINT_G_HASH_TABLE_ZN_PAIR:
                print_g_hash_table_zn_pair(key, value); break;
            case PRINT_G_HASH_TABLE_ZN_PAIR_NODE:
                print_g_hash_table_zn_pair_node(key, value); break;
            case PRINT_G_HASH_TABLE_PROM_LRU_NODE:
                print_g_hash_table_prom_lru_node(key, value); break;
            default:
                printf("Unimplemented hash table print type"); break;
        }
    }

    puts("");
}

inline static void
print_g_queue_zn_zone(GList *node) {
    struct zn_zone *zn = (struct zn_zone *) node->data;
    char *state_str;
    switch (zn->state) {
        case ZN_ZONE_FREE:
            state_str = "FREE"; break;
        case ZN_ZONE_FULL:
            state_str = "FULL"; break;
        case ZN_ZONE_ACTIVE:
            state_str = "ACTIVE"; break;
        case ZN_ZONE_WRITE_OCCURING:
            state_str = "WRITE_OCCURING"; break;
        default:
            assert(!"Invalid zone state");
    }
    printf("(%d,%d,%s), ", zn->zone_id, zn->chunk_offset, state_str);
}

inline static void
print_g_queue_zn_pair(GList *node) {
    struct zn_pair *zn = (struct zn_pair *) node->data;
    printf("(%u,%u,%u,%s), ", zn->zone, zn->chunk_offset, zn->id, zn->in_use ? "true" : "false");
}

void
print_g_queue(char *name, const GQueue *queue, const enum print_g_queue_type type) {
    printf("Printing queue %s: ", name);
    for (GList *node = queue->head; node != NULL; node = node->next) {
        switch (type) {
            case PRINT_G_QUEUE_ZN_ZONE:
                print_g_queue_zn_zone(node); break;
            case PRINT_G_QUEUE_ZN_PAIR:
                print_g_queue_zn_pair(node); break;
            case PRINT_G_QUEUE_GINT:
                printf("%d ", GPOINTER_TO_INT(node->data)); break;
            default:
                printf("Uninimplemented queue print type"); break;
        }
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