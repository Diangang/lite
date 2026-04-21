#include "linux/errno.h"
#include "linux/radix-tree.h"
#include "linux/slab.h"
#include "linux/string.h"

static struct radix_tree_node *radix_tree_node_alloc(gfp_t gfp_mask, unsigned int height)
{
    struct radix_tree_node *node = kmalloc(sizeof(*node));
    if (!node)
        return NULL;
    memset(node, 0, sizeof(*node));
    node->height = height;
    (void)gfp_mask;
    return node;
}

static unsigned long radix_tree_maxindex(unsigned int height)
{
    unsigned long max = 0;
    while (height--)
        max = (max << RADIX_TREE_MAP_SHIFT) | RADIX_TREE_MAP_MASK;
    return max;
}

static unsigned int radix_tree_required_height(unsigned long index)
{
    unsigned int height = 1;
    while (index > radix_tree_maxindex(height))
        height++;
    return height;
}

static int radix_tree_extend(struct radix_tree_root *root, unsigned long index)
{
    unsigned int required = radix_tree_required_height(index);

    if (!root->rnode) {
        root->rnode = radix_tree_node_alloc(root->gfp_mask, required);
        if (!root->rnode)
            return -ENOMEM;
        root->height = required;
        return 0;
    }

    while (root->height < required) {
        struct radix_tree_node *node =
            radix_tree_node_alloc(root->gfp_mask, root->height + 1);
        if (!node)
            return -ENOMEM;
        node->slots[0] = root->rnode;
        node->count = 1;
        root->rnode = node;
        root->height++;
    }

    return 0;
}

int radix_tree_insert(struct radix_tree_root *root, unsigned long index, void *item)
{
    struct radix_tree_node *node;
    unsigned int height;

    if (!root)
        return -EINVAL;
    if (radix_tree_extend(root, index) != 0)
        return -ENOMEM;

    node = root->rnode;
    height = root->height;
    while (height > 1) {
        unsigned int shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
        unsigned int offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        if (!node->slots[offset]) {
            node->slots[offset] = radix_tree_node_alloc(root->gfp_mask, height - 1);
            if (!node->slots[offset])
                return -ENOMEM;
            node->count++;
        }
        node = node->slots[offset];
        height--;
    }

    if (node->slots[index & RADIX_TREE_MAP_MASK])
        return -EBUSY;

    node->slots[index & RADIX_TREE_MAP_MASK] = item;
    node->count++;
    return 0;
}

void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index)
{
    struct radix_tree_node *node;
    unsigned int height;

    if (!root || !root->rnode || index > radix_tree_maxindex(root->height))
        return NULL;

    node = root->rnode;
    height = root->height;
    while (height > 1) {
        unsigned int shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
        unsigned int offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        node = node->slots[offset];
        if (!node)
            return NULL;
        height--;
    }

    return node->slots[index & RADIX_TREE_MAP_MASK];
}

void *radix_tree_delete(struct radix_tree_root *root, unsigned long index)
{
    struct radix_tree_node *path[8];
    unsigned int offsets[8];
    struct radix_tree_node *node;
    unsigned int height;
    void *item;

    if (!root || !root->rnode || index > radix_tree_maxindex(root->height))
        return NULL;

    node = root->rnode;
    height = root->height;
    path[0] = node;
    while (height > 1) {
        unsigned int level = root->height - height + 1;
        unsigned int shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
        unsigned int offset = (index >> shift) & RADIX_TREE_MAP_MASK;
        offsets[level - 1] = offset;
        node = node->slots[offset];
        if (!node)
            return NULL;
        path[level] = node;
        height--;
    }

    offsets[root->height - 1] = index & RADIX_TREE_MAP_MASK;
    item = node->slots[offsets[root->height - 1]];
    if (!item)
        return NULL;
    node->slots[offsets[root->height - 1]] = NULL;
    if (node->count)
        node->count--;

    while (root->height > 0) {
        struct radix_tree_node *cur = path[root->height - 1];
        if (!cur || cur->count)
            break;
        if (root->height == 1) {
            kfree(cur);
            root->rnode = NULL;
            root->height = 0;
            break;
        }
        path[root->height - 2]->slots[offsets[root->height - 2]] = NULL;
        if (path[root->height - 2]->count)
            path[root->height - 2]->count--;
        kfree(cur);
        root->height--;
    }

    if (root->rnode)
        root->height = root->rnode->height;
    return item;
}

static void radix_tree_free_node(struct radix_tree_node *node)
{
    unsigned int i;

    if (!node)
        return;
    if (node->height > 1) {
        for (i = 0; i < RADIX_TREE_MAP_SIZE; i++)
            radix_tree_free_node(node->slots[i]);
    }
    kfree(node);
}

void radix_tree_destroy(struct radix_tree_root *root)
{
    if (!root)
        return;
    radix_tree_free_node(root->rnode);
    root->rnode = NULL;
    root->height = 0;
}
