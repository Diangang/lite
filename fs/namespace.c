#include "fs.h"
#include "ramfs.h"
#include "kheap.h"
#include "libc.h"
#include "console.h"

static struct file_system_type *file_systems;

int register_filesystem(struct file_system_type *fs)
{
    struct file_system_type **p;
    if (!fs)
        return -1;

    for (p = &file_systems; *p; p = &(*p)->next) {
        if (strcmp((*p)->name, fs->name) == 0)
            return -1; // Already registered
    }

    fs->next = NULL;
    *p = fs;
    return 0;
}

int unregister_filesystem(struct file_system_type *fs)
{
    struct file_system_type **p;
    if (!fs)
        return -1;

    for (p = &file_systems; *p; p = &(*p)->next) {
        if (*p == fs) {
            *p = fs->next;
            return 0;
        }
    }
    return -1;
}

int vfs_mount_root_fs(const char *fs_name)
{
    struct file_system_type *fs;
    for (fs = file_systems; fs; fs = fs->next) {
        if (strcmp(fs->name, fs_name) == 0)
            break;
    }

    if (!fs)
        panic("Root filesystem type not found.");

    struct super_block *sb = fs->get_sb(fs, 0, NULL, NULL);
    if (!sb)
        panic("Failed to get root super_block.");

    struct dentry *root_dentry = d_alloc(NULL, "/");
    root_dentry->inode = sb->root;

    vfs_root_dentry = root_dentry;

    struct vfsmount *m = (struct vfsmount*)kmalloc(sizeof(struct vfsmount));
    m->path = "/";
    m->sb = sb;
    m->root = root_dentry;

    root_dentry->mount = m;

    printf("mount / (%s) success.\n", fs_name);
    return 0;
}

int vfs_mount_root(const char *path, struct dentry *mount_root)
{
    if (!path || !mount_root || !mount_root->inode)
        panic("mount path or root dentry null.");

    struct dentry *d = NULL;

    if (strcmp(path, "/") == 0) {
        d = mount_root;
    } else {
        d = path_walk(path);
        if (!d) {
            if (vfs_mkdir(path) != 0)
                panic("mount path missing and could not be created.");
            d = path_walk(path);
            if (!d)
                panic("mount path still missing after creation.");
        }

        // Link the new file system root dentry into the namespace topology.
        // We manually set the parent to allow "cd .." to escape the mount point.
        // We intentionally DO NOT use d_alloc(d->parent) because that would
        // inject this new root into the parent's children list, causing duplicate ls entries!
        mount_root->parent = d->parent;
    }

    struct vfsmount *m = (struct vfsmount*)kmalloc(sizeof(struct vfsmount));
    if (!m)
        panic("mount path failed for alloc.");

    m->path = strdup(path);
    m->sb = (struct super_block*)kmalloc(sizeof(struct super_block));
    if (!m->sb)
        panic("mount path failed for alloc sb.");

    m->sb->name = m->path;
    m->sb->refcount = 1;
    m->sb->root = mount_root->inode;
    m->sb->fs_private = NULL;

    m->root = mount_root;
    d->mount = m;

    printf("mount %s success.\n", path);
    return 0;
}

static void init_mount_tree(void)
{
    vfs_mount_root_fs("ramfs");
}

void vfs_init(void)
{
    // 1. Initialize VFS caches (if we had them properly separated)

    // 2. Register rootfs, which is ramfs.
    // Here we register our ramfs to act as the rootfs.
    init_ramfs_fs();

    // 3. Mount the root filesystem
    init_mount_tree();
}
