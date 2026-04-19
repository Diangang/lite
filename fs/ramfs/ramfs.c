#include "linux/ramfs.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/cred.h"
#include "linux/pagemap.h"

// Ramfs relies entirely on generic_file_read/write and generic_readdir
// We don't even need a private data struct anymore!
enum { RAMFS_MAGIC = 0x52414D46 };

/* ramfs_apply_umask: Implement ramfs apply umask. */
static struct inode_operations ramfs_dir_iops;
static struct file_operations ramfs_dir_ops;
static struct file_operations ramfs_file_ops;
static struct file_operations ramfs_symlink_ops;

static uint32_t ramfs_apply_umask(uint32_t mode);

/*
 * Linux mapping:
 * - inode creation funnels through ramfs_get_inode() (Linux: ramfs_get_inode()).
 * - inode teardown funnels through ramfs_evict_inode() (Linux: evict_inode()).
 *
 * Lite does not have a global inode cache/evict path, so ramfs calls the
 * teardown helper from unlink/rmdir where inodes are actually freed.
 */
static struct inode *ramfs_get_inode(struct inode *dir, uint32_t type, uint32_t mode)
{
    (void)dir;
    struct inode *inode = (struct inode *)kmalloc(sizeof(*inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));

    inode->i_ino = get_next_ino();
    inode->uid = task_get_uid();
    inode->gid = task_get_gid();

    if (type == FS_DIRECTORY) {
        struct address_space *mapping = (struct address_space *)kmalloc(sizeof(*mapping));
        if (!mapping) {
            kfree(inode);
            return NULL;
        }
        address_space_init(mapping, inode);
        inode->i_mapping = mapping;
        inode->flags = FS_DIRECTORY;
        inode->i_op = &ramfs_dir_iops;
        inode->f_ops = &ramfs_dir_ops;
        inode->i_mode = ramfs_apply_umask(mode ? mode : 0777);
        return inode;
    }

    if (type == FS_SYMLINK) {
        inode->flags = FS_SYMLINK;
        inode->i_op = NULL;
        inode->f_ops = &ramfs_symlink_ops;
        inode->i_mode = 0777;
        inode->i_size = 0;
        return inode;
    }

    /* Regular files by default. */
    struct address_space *mapping = (struct address_space *)kmalloc(sizeof(*mapping));
    if (!mapping) {
        kfree(inode);
        return NULL;
    }
    address_space_init(mapping, inode);
    inode->i_mapping = mapping;
    inode->flags = FS_FILE;
    inode->i_op = NULL;
    inode->f_ops = &ramfs_file_ops;
    inode->i_size = 0;
    inode->i_mode = ramfs_apply_umask(mode ? mode : 0666);
    return inode;
}

static void ramfs_evict_inode(struct inode *inode)
{
    if (!inode)
        return;

    if ((inode->flags & 0x7) != FS_DIRECTORY) {
        /* Truncate file mapping to free physical pages. */
        if (inode->i_mapping)
            truncate_inode_pages(inode->i_mapping, 0);
    }

    if (inode->private_data) {
        kfree(inode->private_data);
        inode->private_data = NULL;
    }
    if (inode->i_mapping) {
        address_space_release(inode->i_mapping);
        kfree(inode->i_mapping);
        inode->i_mapping = NULL;
    }
    kfree(inode);
}

static uint32_t ramfs_apply_umask(uint32_t mode)
{
    uint32_t mask = task_get_umask();
    return mode & (~mask) & 0777;
}

/* ramfs_valid_name: Implement ramfs valid name. */
static int ramfs_valid_name(const char *name)
{
    if (!name || !*name)
        return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '/')
            return 0;
    }
    return 1;
}

static uint32_t ramfs_symlink_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || node->flags != FS_SYMLINK || !node->private_data)
        return 0;
    const char *target = (const char *)node->private_data;
    uint32_t len = (uint32_t)strlen(target);
    if (offset >= len)
        return 0;
    uint32_t remain = len - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, target + offset, size);
    return size;
}

/* ramfs_create_child: Implement ramfs create child. */
struct inode *ramfs_create_child(struct inode *dir, const char *name, uint32_t type)
{
    if (!dir || !name)
        return NULL;
    if (!ramfs_valid_name(name))
        return NULL;
    if ((dir->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    // Actually, `ramfs_create_child` is called by `vfs_open` and `vfs_mkdir` when it wants to create a file.
    // They already handle the dcache! So `ramfs_create_child` just needs to return a new bare inode!
    if (type == FS_DIRECTORY)
        return ramfs_get_inode(dir, FS_DIRECTORY, 0777);
    if (type == FS_SYMLINK)
        return ramfs_get_inode(dir, FS_SYMLINK, 0777);
    return ramfs_get_inode(dir, FS_FILE, 0666);
}

/* ramfs_unlink: Implement ramfs unlink. */
static int ramfs_unlink(struct dentry *dir_dentry, const char *name)
{
    if (!dir_dentry || !name)
        return -1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;
    struct inode *dir = dir_dentry->inode;
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY)
        return -1;

    struct dentry *child = dir_dentry->children;
    struct dentry *found = NULL;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            found = child;
            break;
        }
        child = child->sibling;
    }

    if (!found)
        return -1; // File not found

    struct inode *target = found->inode;
    if (!target)
        return -1;

    if ((target->flags & 0x7) == FS_DIRECTORY) {
        // Simple protection: don't unlink directories, use rmdir
        return -1;
    }
    vfs_dentry_detach(found);
    if (found->name)
        kfree((void*)found->name);
    kfree(found);
    ramfs_evict_inode(target);

    return 0;
}

/* ramfs_rmdir: Implement ramfs rmdir. */
static int ramfs_rmdir(struct dentry *dir_dentry, const char *name)
{
    if (!dir_dentry || !name)
        return -1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;
    struct inode *dir = dir_dentry->inode;
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY)
        return -1;

    struct dentry *child = dir_dentry->children;
    struct dentry *found = NULL;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            found = child;
            break;
        }
        child = child->sibling;
    }

    if (!found)
        return -1;

    struct inode *target = found->inode;
    if (!target)
        return -1;
    if ((target->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (found->children)
        return -1;
    vfs_dentry_detach(found);
    if (found->name)
        kfree((void*)found->name);
    kfree(found);
    ramfs_evict_inode(target);

    return 0;
}

static struct inode *ramfs_lookup(struct inode *dir, const char *name)
{
    (void)dir;
    return generic_finddir(dir, name);
}

static struct inode *ramfs_create_file(struct inode *dir, const char *name)
{
    return ramfs_create_child(dir, name, FS_FILE);
}

static struct inode *ramfs_mkdir_inode(struct inode *dir, const char *name)
{
    return ramfs_create_child(dir, name, FS_DIRECTORY);
}

static struct inode *ramfs_symlink_inode(struct inode *dir, const char *name, const char *target)
{
    if (!target)
        return NULL;
    struct inode *inode = ramfs_create_child(dir, name, FS_SYMLINK);
    if (!inode)
        return NULL;
    uint32_t len = (uint32_t)strlen(target);
    char *copy = (char *)kmalloc(len + 1);
    if (!copy) {
        kfree(inode);
        return NULL;
    }
    memcpy(copy, target, len + 1);
    inode->private_data = copy;
    inode->i_size = len;
    return inode;
}

static struct inode_operations ramfs_dir_iops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create_file,
    .mkdir = ramfs_mkdir_inode,
    .symlink = ramfs_symlink_inode,
    .unlink = ramfs_unlink,
    .rmdir = ramfs_rmdir,
};

static struct file_operations ramfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = generic_readdir,
    .ioctl = NULL,
};

static struct file_operations ramfs_file_ops = {
    .read = generic_file_read,
    .write = generic_file_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations ramfs_symlink_ops = {
    .read = ramfs_symlink_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

/* ramfs_fill_super: Implement ramfs fill super. */
int ramfs_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    struct inode *inode = ramfs_get_inode(NULL, FS_DIRECTORY, 0755);
    if (!inode)
        return -1;
    /* Keep Linux-like special root ino value. */
    inode->i_ino = 1;
    inode->uid = 0;
    inode->gid = 0;

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root) {
        ramfs_evict_inode(inode);
        return -1;
    }
    sb->s_root->inode = inode;

    return 0;
}

static struct file_system_type ramfs_fs_type = {
    .name = "ramfs",
    .get_sb = vfs_get_sb_single,
    .fill_super = ramfs_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* init_ramfs_fs: Initialize ramfs fs. */
int init_ramfs_fs(void)
{
    register_filesystem(&ramfs_fs_type);
    printf("ramfs filesystem registered.\n");
    return 0;
}
