#include "linux/errno.h"
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
