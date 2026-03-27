#include "linux/fs.h"
#include "linux/ramfs.h"
#include "linux/kheap.h"
#include "linux/libc.h"
#include "linux/console.h"

static struct file_system_type *file_systems;
static struct vfsmount *vfs_mounts;

struct vfsmount *vfs_get_mounts(void)
{
    return vfs_mounts;
}

static struct file_system_type *get_filesystem(const char *name)
{
    if (!name)
        return NULL;
    for (struct file_system_type *fs = file_systems; fs; fs = fs->next) {
        if (strcmp(fs->name, name) == 0)
            return fs;
    }
    return NULL;
}

struct super_block *vfs_get_sb_single(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    (void)flags;
    (void)dev_name;

    if (!fs_type)
        panic("vfs_get_sb_single: fs_type null.");
    if (!fs_type->fill_super)
        panic("vfs_get_sb_single: fill_super missing.");

    struct super_block *sb = (struct super_block*)kmalloc(sizeof(struct super_block));
    if (!sb)
        panic("vfs_get_sb_single: alloc sb failed.");
    memset(sb, 0, sizeof(*sb));

    sb->name = fs_type->name;
    sb->refcount = 1;

    int ret = fs_type->fill_super(sb, data, 0);
    if (ret != 0)
        panic("vfs_get_sb_single: fill_super failed.");

    if (!sb->s_root || !sb->s_root->inode)
        panic("vfs_get_sb_single: root dentry missing.");

    return sb;
}

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

static int vfs_mount_rootfs(const char *fs_name)
{
    struct file_system_type *fs = get_filesystem(fs_name);

    if (!fs)
        panic("Root filesystem type not found.");

    struct super_block *sb = fs->get_sb(fs, 0, NULL, NULL);
    if (!sb)
        panic("Failed to get root super_block.");

    if (!sb->s_root || !sb->s_root->inode)
        panic("Root super_block has no root dentry.");

    vfs_root_dentry = sb->s_root;

    struct vfsmount *m = (struct vfsmount*)kmalloc(sizeof(struct vfsmount));
    m->path = "/";
    m->sb = sb;
    m->root = sb->s_root;
    m->next = vfs_mounts;
    vfs_mounts = m;

    sb->s_root->mount = m;

    printf("mount / (%s) success.\n", fs_name);
    return 0;
}

int vfs_mount(const char *path, struct super_block *sb)
{
    if (!path || !sb || !sb->s_root || !sb->s_root->inode)
        panic("mount path or super_block null.");

    struct dentry *mount_root = sb->s_root;
    struct dentry *d = path_walk(path);
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

    struct vfsmount *m = (struct vfsmount*)kmalloc(sizeof(struct vfsmount));
    if (!m)
        panic("mount path failed for alloc.");

    m->path = strdup(path);
    m->sb = sb;

    m->root = mount_root;
    m->next = vfs_mounts;
    vfs_mounts = m;
    d->mount = m;

    printf("mount %s success.\n", path);
    return 0;
}

int vfs_mount_fs(const char *path, const char *fs_name)
{
    struct file_system_type *fs = get_filesystem(fs_name);
    if (!fs)
        panic("filesystem type not found.");

    struct super_block *sb = fs->get_sb(fs, 0, NULL, NULL);
    if (!sb)
        panic("get_sb failed.");

    return vfs_mount(path, sb);
}

void vfs_init(void)
{
    // 1. Initialize VFS caches (if we had them properly separated)

    // 2. Register rootfs, which is ramfs.
    // Here we register our ramfs to act as the rootfs.
    init_ramfs_fs();

    // 3. Mount the root filesystem
    vfs_mount_rootfs("ramfs");
}
