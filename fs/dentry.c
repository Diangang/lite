#include "fs.h"
#include "kheap.h"
#include "libc.h"

struct dentry *vfs_root_dentry = NULL;

struct dentry *d_alloc(struct dentry *parent, const char *name)
{
    struct dentry *d = (struct dentry*)kmalloc(sizeof(struct dentry));
    if (!d)
        return NULL;
    memset(d, 0, sizeof(*d));
    d->name = name ? strdup(name) : NULL;
    d->parent = parent;
    d->refcount = 1;
    if (parent) {
        d->sibling = parent->children;
        parent->children = d;
    }
    return d;
}

struct dentry *d_lookup(struct dentry *parent, const char *name)
{
    if (!parent || !name)
        return NULL;
    struct dentry *c = parent->children;
    while (c) {
        if (c->name && strcmp(c->name, name) == 0)
            return c;
        c = c->sibling;
    }
    return NULL;
}

struct dentry *vfs_dentry_get(struct inode *node, const char *name)
{
    // For legacy compat where code just wanted a dummy dentry for an inode
    // Not recommended for new dcache
    struct dentry *d = d_alloc(NULL, name);
    d->inode = node;
    return d;
}

void vfs_dentry_put(struct dentry *d)
{
    if (!d)
        return;
    if (d->refcount > 0) d->refcount--;
    // Simple dcache: we never actually free them to keep tree intact,
    // unless system is out of memory.
}
