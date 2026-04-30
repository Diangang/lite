#ifndef _LINUX_RADIX_TREE_H
#define _LINUX_RADIX_TREE_H

#include "linux/gfp.h"
#include "linux/types.h"

/*
 * Linux mapping: radix-tree objects live in include/linux/radix-tree.h.
 * Lite implements a minimal non-RCU subset sufficient for IDR backing.
 */

#define RADIX_TREE_MAP_SHIFT 6
#define RADIX_TREE_MAP_SIZE (1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK (RADIX_TREE_MAP_SIZE - 1)
#define RADIX_TREE_INDEX_BITS (8 * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH \
    ((RADIX_TREE_INDEX_BITS + RADIX_TREE_MAP_SHIFT - 1) / RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_HEIGHT_SHIFT (RADIX_TREE_MAX_PATH + 1)
#define RADIX_TREE_HEIGHT_MASK ((1UL << RADIX_TREE_HEIGHT_SHIFT) - 1)
#define RADIX_TREE_COUNT_SHIFT (RADIX_TREE_MAP_SHIFT + 1)
#define RADIX_TREE_COUNT_MASK ((1UL << RADIX_TREE_COUNT_SHIFT) - 1)

struct radix_tree_node {
    unsigned int height;
    unsigned int count;
    void *slots[RADIX_TREE_MAP_SIZE];
};

struct radix_tree_root {
    unsigned int height;
    gfp_t gfp_mask;
    struct radix_tree_node *rnode;
};

#define RADIX_TREE_INIT(mask) { .height = 0, .gfp_mask = (mask), .rnode = NULL }
#define RADIX_TREE(name, mask) struct radix_tree_root name = RADIX_TREE_INIT(mask)

#define INIT_RADIX_TREE(root, mask)      \
    do {                                 \
        (root)->height = 0;              \
        (root)->gfp_mask = (mask);       \
        (root)->rnode = NULL;            \
    } while (0)

static inline void *radix_tree_deref_slot(void **pslot)
{
    return *pslot;
}

static inline void radix_tree_replace_slot(void **pslot, void *item)
{
    *pslot = item;
}

int radix_tree_insert(struct radix_tree_root *root, unsigned long index, void *item);
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index);
void **radix_tree_lookup_slot(struct radix_tree_root *root, unsigned long index);
void *radix_tree_delete_item(struct radix_tree_root *root, unsigned long index, void *item);
void *radix_tree_delete(struct radix_tree_root *root, unsigned long index);
void radix_tree_destroy(struct radix_tree_root *root);

#endif
