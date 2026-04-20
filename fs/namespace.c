#include "linux/fs.h"
#include "linux/namei.h"
#include "linux/ramfs.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/console.h"

/* vfs_get_mounts: Return the current mount list. */
static struct file_system_type *file_systems;
static struct vfsmount *vfs_mounts;

struct vfsmount *vfs_get_mounts(void)
{
    return vfs_mounts;
}

/* get_fs_type: Linux-style filesystem type lookup. */
static struct file_system_type *get_fs_type(const char *name)
{
    if (!name)
        return NULL;
    for (struct file_system_type *fs = file_systems; fs; fs = fs->next) {
        if (strcmp(fs->name, name) == 0)
            return fs;
    }
    return NULL;
}

/* vfs_get_sb_single: Implement vfs get sb single. */
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

/* register_filesystem: Register filesystem. */
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

/* unregister_filesystem: Unregister filesystem. */
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

/* init_mount_tree: Install the initial root mount. */
static int init_mount_tree(void)
{
    const char *fs_name = "ramfs";
    struct file_system_type *fs = get_fs_type(fs_name);

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
    m->mountpoint = NULL;
    m->parent = NULL;
    m->next = vfs_mounts;
    vfs_mounts = m;

    sb->s_root->mount = m;

    printf("mount / (%s) success.\n", fs_name);
    return 0;
}

/* vfs_mount: Implement vfs mount. */
int vfs_mount(const char *path, struct super_block *sb)
{
    if (!path || !sb || !sb->s_root || !sb->s_root->inode)
        panic("mount path or super_block null.");

    struct dentry *mount_root = sb->s_root;
    struct path lookup;
    struct dentry *d = NULL;
    if (kern_path(path, LOOKUP_DIRECTORY, &lookup) == 0)
        d = lookup.dentry;
    if (!d) {
        if (vfs_mkdir(path) != 0)
            panic("mount path missing and could not be created.");
        if (kern_path(path, LOOKUP_DIRECTORY, &lookup) == 0)
            d = lookup.dentry;
        if (!d)
            panic("mount path still missing after creation.");
    }

    struct vfsmount *m = (struct vfsmount*)kmalloc(sizeof(struct vfsmount));
    if (!m)
        panic("mount path failed for alloc.");

    m->path = kstrdup(path);
    m->sb = sb;

    m->root = mount_root;
    m->mountpoint = d;
    m->parent = d->mount ? d->mount : (vfs_mounts ? vfs_mounts : NULL);
    m->next = vfs_mounts;
    vfs_mounts = m;
    d->mount = m;
    /* Mark the mounted filesystem root for ".." traversal across mounts. */
    mount_root->mount = m;

    printf("mount %s success.\n", path);
    return 0;
}

/* vfs_mount_fs: Implement vfs mount fs. */
int vfs_mount_fs(const char *path, const char *fs_name)
{
    struct file_system_type *fs = get_fs_type(fs_name);
    if (!fs)
        panic("filesystem type not found.");

    struct super_block *sb = fs->get_sb(fs, 0, NULL, NULL);
    if (!sb)
        panic("get_sb failed.");

    return vfs_mount(path, sb);
}

/* vfs_mount_fs_dev: Implement vfs mount fs dev. */
int vfs_mount_fs_dev(const char *path, const char *fs_name, const char *dev_name)
{
    struct file_system_type *fs = get_fs_type(fs_name);
    if (!fs)
        panic("filesystem type not found.");

    /* #region debug-point scsi-nvme-smoke.mount-fs-dev */
    printf("debug(scsi-nvme-smoke): vfs_mount_fs_dev path=%s fs=%s dev=%s\n",
           path ? path : "(null)",
           fs_name ? fs_name : "(null)",
           dev_name ? dev_name : "(null)");
    /* #endregion */

    struct super_block *sb = fs->get_sb(fs, 0, dev_name, NULL);
    if (!sb)
        panic("get_sb failed.");

    return vfs_mount(path, sb);
}

/* vfs_init: Initialize vfs. */
void vfs_init(void)
{
    // 1. Initialize VFS caches (if we had them properly separated)

    // 2. Register rootfs, which is ramfs.
    // Here we register our ramfs to act as the rootfs.
    init_ramfs_fs();

    // 3. Mount the root filesystem
    init_mount_tree();
}
