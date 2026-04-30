#include "linux/errno.h"
#include "linux/err.h"
#include "linux/idr.h"
#include "linux/radix-tree.h"
#include "linux/slab.h"
#include <limits.h>

void idr_init(struct idr *idp)
{
    if (!idp)
        return;
    INIT_RADIX_TREE(&idp->root, GFP_KERNEL);
    idp->cur = 0;
}

int idr_pre_get(struct idr *idp, unsigned int gfp_mask)
{
    (void)idp;
    (void)gfp_mask;
    return 1;
}

void idr_preload(gfp_t gfp_mask)
{
    (void)gfp_mask;
}

int idr_alloc(struct idr *idp, void *ptr, int start, int end, gfp_t gfp_mask)
{
    int id;

    if (!idp || start < 0)
        return -EINVAL;
    if (end <= 0)
        end = INT_MAX;
    if (start >= end)
        return -EINVAL;

    idp->root.gfp_mask = gfp_mask;
    for (id = start; id < end; id++) {
        if (!radix_tree_lookup(&idp->root, (unsigned long)id)) {
            if (radix_tree_insert(&idp->root, (unsigned long)id, ptr) != 0)
                return -ENOMEM;
            idp->cur = id + 1;
            return id;
        }
    }

    return -ENOSPC;
}

int idr_alloc_cyclic(struct idr *idp, void *ptr, int start, int end, gfp_t gfp_mask)
{
    int id;
    int next;

    if (!idp)
        return -EINVAL;

    next = idp->cur > start ? idp->cur : start;
    id = idr_alloc(idp, ptr, next, end, gfp_mask);
    if (id == -ENOSPC)
        id = idr_alloc(idp, ptr, start, end, gfp_mask);

    if (id >= 0)
        idp->cur = id + 1;
    return id;
}

int idr_get_new_above(struct idr *idp, void *ptr, int starting_id, int *id)
{
    int ret;

    if (!id)
        return -EINVAL;
    ret = idr_alloc(idp, ptr, starting_id < 0 ? 0 : starting_id, 0, GFP_KERNEL);
    if (ret < 0)
        return ret;
    *id = ret;
    return 0;
}

int idr_get_new(struct idr *idp, void *ptr, int *id)
{
    int ret;

    if (!idp || !id)
        return -EINVAL;
    ret = idr_alloc(idp, ptr, idp->cur, 0, GFP_KERNEL);
    if (ret < 0 && idp->cur > 0)
        ret = idr_alloc(idp, ptr, 0, idp->cur, GFP_KERNEL);
    if (ret < 0)
        return ret;
    *id = ret;
    return 0;
}

void *idr_find(struct idr *idp, int id)
{
    if (!idp || id < 0)
        return NULL;
    return radix_tree_lookup(&idp->root, (unsigned long)id);
}

static void *idr_get_next_node(struct radix_tree_node *node, unsigned int height,
                               unsigned long base, unsigned long start,
                               int *nextidp)
{
    unsigned int offset;

    if (!node)
        return NULL;

    if (height == 1) {
        unsigned int first = start > base ? (unsigned int)(start - base) : 0;
        for (offset = first; offset < RADIX_TREE_MAP_SIZE; offset++) {
            unsigned long id = base + offset;
            if (id > INT_MAX)
                return NULL;
            if (node->slots[offset]) {
                *nextidp = (int)id;
                return node->slots[offset];
            }
        }
        return NULL;
    }

    for (offset = 0; offset < RADIX_TREE_MAP_SIZE; offset++) {
        unsigned long span = 1UL << ((height - 1) * RADIX_TREE_MAP_SHIFT);
        unsigned long child_base = base + offset * span;

        if (child_base + span - 1 < start)
            continue;
        if (child_base > INT_MAX)
            return NULL;
        if (node->slots[offset]) {
            void *ptr = idr_get_next_node(node->slots[offset], height - 1,
                                          child_base, start, nextidp);
            if (ptr)
                return ptr;
        }
    }

    return NULL;
}

void *idr_get_next(struct idr *idp, int *nextidp)
{
    if (!idp || !nextidp)
        return NULL;
    if (*nextidp < 0)
        return NULL;
    return idr_get_next_node(idp->root.rnode, idp->root.height, 0,
                             (unsigned long)*nextidp, nextidp);
}

int idr_for_each(struct idr *idp, int (*fn)(int id, void *p, void *data), void *data)
{
    int id = 0;
    void *ptr;

    if (!idp || !fn)
        return 0;

    while ((ptr = idr_get_next(idp, &id)) != NULL) {
        int ret = fn(id, ptr, data);
        if (ret)
            return ret;
        if (id == INT_MAX)
            break;
        id++;
    }

    return 0;
}

void *idr_replace(struct idr *idp, void *ptr, int id)
{
    void **slot;
    void *old;

    if (!idp || id < 0)
        return ERR_PTR(-EINVAL);

    slot = radix_tree_lookup_slot(&idp->root, (unsigned long)id);
    if (!slot)
        return ERR_PTR(-ENOENT);

    old = *slot;
    *slot = ptr;
    return old;
}

void idr_remove(struct idr *idp, int id)
{
    if (!idp || id < 0)
        return;
    if (radix_tree_delete(&idp->root, (unsigned long)id) && id < idp->cur)
        idp->cur = id;
}

void idr_destroy(struct idr *idp)
{
    if (!idp)
        return;
    radix_tree_destroy(&idp->root);
    idp->cur = 0;
}

bool idr_is_empty(struct idr *idp)
{
    return !idp || idp->root.rnode == NULL;
}
