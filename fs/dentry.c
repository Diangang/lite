#include "vfs.h"
#include "kheap.h"
#include "libc.h"

struct vfs_icache_entry {
    struct vfs_inode *node;
    struct vfs_inode inode;
    struct vfs_dentry dentry;
    uint32_t refs;
    struct vfs_icache_entry *next;
};
static struct vfs_icache_entry *vfs_icache_head = NULL;

struct vfs_dentry *vfs_dentry_get(struct vfs_inode *node, const char *name)
{
    if (!node) return NULL;
    struct vfs_icache_entry *e = vfs_icache_head;
    while (e) {
        if (e->node == node) {
            e->refs++;
            e->inode.refcount++;
            e->dentry.refcount++;
            return &e->dentry;
        }
        e = e->next;
    }
    e = (struct vfs_icache_entry*)kmalloc(sizeof(struct vfs_icache_entry));
    if (!e) return NULL;
    memset(e, 0, sizeof(*e));
    e->node = node;
    e->inode.f_ops = node->f_ops;
    e->inode.private_data = node->private_data;
    e->inode.mask = node->mask;
    e->inode.uid = node->uid;
    e->inode.gid = node->gid;
    e->inode.flags = node->flags;
    e->inode.inode = node->inode;
    e->inode.length = node->length;
    e->inode.impl = node->impl;
    e->dentry.name = name ? strdup(name) : NULL;
    e->dentry.parent = NULL;
    e->dentry.inode = &e->inode;
    e->dentry.refcount = 1;
    e->dentry.cache = e;
    e->refs = 1;
    e->next = vfs_icache_head;
    vfs_icache_head = e;
    return &e->dentry;
}

void vfs_dentry_put(struct vfs_dentry *d)
{
    if (!d) return;
    struct vfs_icache_entry *e = (struct vfs_icache_entry*)d->cache;
    if (!e) return;
    if (e->refs > 0) e->refs--;
    if (e->inode.refcount > 0) e->inode.refcount--;
    if (e->dentry.refcount > 0) e->dentry.refcount--;
    if (e->refs > 0) return;
    struct vfs_icache_entry **pp = &vfs_icache_head;
    while (*pp) {
        if (*pp == e) {
            *pp = e->next;
            break;
        }
        pp = &(*pp)->next;
    }
    if (e->dentry.name) kfree((void*)e->dentry.name);
    kfree(e);
}
