#include "linux/fs.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include <stdint.h>

struct dentry *vfs_root_dentry = NULL;

#define D_HASH_SIZE 32

struct dcache_hash {
    struct dentry *buckets[D_HASH_SIZE];
};

static uint32_t d_hash_name(const char *name)
{
    uint32_t h = 5381u;
    if (!name)
        return 0;
    while (*name)
        h = ((h << 5) + h) + (uint8_t)(*name++);
    return h;
}

static struct dcache_hash *d_hash_get_or_alloc(struct dentry *parent)
{
    if (!parent)
        return NULL;
    if (!parent->cache) {
        struct dcache_hash *h = (struct dcache_hash *)kmalloc(sizeof(*h));
        if (!h)
            return NULL;
        memset(h, 0, sizeof(*h));
        parent->cache = h;
    }
    return (struct dcache_hash *)parent->cache;
}

static void d_hash_insert(struct dentry *d)
{
    if (!d || !d->parent || !d->name)
        return;
    struct dcache_hash *h = d_hash_get_or_alloc(d->parent);
    if (!h)
        return;
    uint32_t idx = d_hash_name(d->name) & (D_HASH_SIZE - 1);
    d->hash_next = h->buckets[idx];
    h->buckets[idx] = d;
}

static void d_hash_remove(struct dentry *d, struct dentry *parent)
{
    if (!d || !parent || !parent->cache || !d->name)
        return;
    struct dcache_hash *h = (struct dcache_hash *)parent->cache;
    uint32_t idx = d_hash_name(d->name) & (D_HASH_SIZE - 1);
    struct dentry *cur = h->buckets[idx];
    struct dentry *prev = NULL;
    while (cur) {
        if (cur == d) {
            if (prev)
                prev->hash_next = cur->hash_next;
            else
                h->buckets[idx] = cur->hash_next;
            cur->hash_next = NULL;
            return;
        }
        prev = cur;
        cur = cur->hash_next;
    }
}

/* d_alloc: Implement d alloc. */
struct dentry *d_alloc(struct dentry *parent, const char *name)
{
    struct dentry *d = (struct dentry*)kmalloc(sizeof(struct dentry));
    if (!d)
        return NULL;
    memset(d, 0, sizeof(*d));
    d->name = name ? kstrdup(name) : NULL;
    d->parent = parent;
    d->refcount = 1;
    d->d_flags = 0;
    if (parent) {
        d->sibling = parent->children;
        parent->children = d;
        d_hash_insert(d);
    }
    return d;
}

/* d_lookup: Implement d lookup. */
struct dentry *d_lookup(struct dentry *parent, const char *name)
{
    if (!parent || !name)
        return NULL;
    if (parent->cache) {
        struct dcache_hash *h = (struct dcache_hash *)parent->cache;
        uint32_t idx = d_hash_name(name) & (D_HASH_SIZE - 1);
        struct dentry *c = h->buckets[idx];
        while (c) {
            if (c->name && strcmp(c->name, name) == 0)
                return c;
            c = c->hash_next;
        }
        return NULL;
    }
    struct dentry *c = parent->children;
    while (c) {
        if (c->name && strcmp(c->name, name) == 0)
            return c;
        c = c->sibling;
    }
    return NULL;
}

/* vfs_dentry_get: Implement vfs dentry get. */
struct dentry *vfs_dentry_get(struct inode *node, const char *name)
{
    // For legacy compat where code just wanted a dummy dentry for an inode
    // Not recommended for new dcache
    struct dentry *d = d_alloc(NULL, name);
    d->inode = node;
    return d;
}

/* vfs_dentry_put: Implement vfs dentry put. */
void vfs_dentry_put(struct dentry *d)
{
    if (!d)
        return;
    if (d->refcount > 0) d->refcount--;
    /*
     * Minimal reclaim policy:
     * - only free dentries that have been detached from the tree and are
     *   unreferenced.
     *
     * Linux mapping: dput() may reclaim unused dentries; Lite does not yet have
     * LRU shrink, so we only reclaim when explicitly detached.
     */
    if (d->refcount != 0)
        return;
    if (d->parent || d->children)
        return;
    if (d->mount)
        return;
    if (d == vfs_root_dentry)
        return;

    if (d->cache)
        kfree(d->cache);
    if (d->name)
        kfree((void *)d->name);
    kfree(d);
}

/* vfs_dentry_detach: Implement vfs dentry detach. */
void vfs_dentry_detach(struct dentry *d)
{
    if (!d)
        return;
    if (!d->parent)
        return;
    struct dentry *parent = d->parent;
    d_hash_remove(d, parent);
    if (parent->children == d) {
        parent->children = d->sibling;
        d->parent = NULL;
        d->sibling = NULL;
        return;
    }
    struct dentry *curr = parent->children;
    while (curr && curr->sibling != d)
        curr = curr->sibling;
    if (curr)
        curr->sibling = d->sibling;
    d->parent = NULL;
    d->sibling = NULL;
}
