#include "fs.h"
#include "kheap.h"
#include "libc.h"
#include "console.h"

int vfs_mount_root(const char *path, struct inode *root_node)
{
    if (!path || !root_node)
        panic("mount path null.");

    if (strcmp(path, "/") == 0) {
        if (!vfs_root_dentry) {
            vfs_root_dentry = d_alloc(NULL, "/");
        }
        vfs_root_dentry->inode = root_node;
        return 0;
    }

    struct dentry *d = path_walk(path);
    if (!d) {
        if (vfs_mkdir(path) != 0) {
            printf("Failed to create mountpoint %s\n", path);
            panic("mount path missing and could not be created.");
        }
        d = path_walk(path);
        if (!d) panic("mount path still missing after creation.");
    }

    struct vfs_mount *m = (struct vfs_mount*)kmalloc(sizeof(struct vfs_mount));
    if (!m) panic("mount path failed for alloc.");

    m->path = strdup(path);
    m->sb = (struct vfs_super_block*)kmalloc(sizeof(struct vfs_super_block));
    if (!m->sb) panic("mount path failed for alloc sb.");

    m->sb->name = m->path;
    m->sb->refcount = 1;
    m->sb->root = root_node;
    m->sb->fs_private = NULL;

    struct dentry *mount_root = d_alloc(NULL, d->name);
    // We manually set the parent to allow "cd .." to escape the mount point,
    // BUT we intentionally DO NOT use d_alloc(d->parent) because that would
    // inject this new root into the parent's children list, causing duplicate ls entries!
    mount_root->parent = d->parent;
    mount_root->inode = root_node;
    m->root = mount_root;

    d->mount = m;

    printf("mount %s success.\n", path);
    return 0;
}
