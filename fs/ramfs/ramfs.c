#include "ramfs.h"
#include "kheap.h"
#include "libc.h"
#include "task.h"

// Ramfs relies entirely on generic_file_read/write and generic_readdir
// We don't even need a private data struct anymore!
enum { RAMFS_MAGIC = 0x52414D46 };

static struct file_operations ramfs_dir_ops;
static struct file_operations ramfs_file_ops;

static uint32_t ramfs_apply_umask(uint32_t mode)
{
    uint32_t mask = task_get_umask();
    return mode & (~mask) & 0777;
}

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

    struct inode *inode = (struct inode*)kmalloc(sizeof(struct inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));

    inode->i_ino = get_next_ino();
    inode->uid = task_get_uid();
    inode->gid = task_get_gid();

    struct address_space *mapping = (struct address_space*)kmalloc(sizeof(struct address_space));
    address_space_init(mapping, inode);
    inode->i_mapping = mapping;

    if (type == FS_DIRECTORY) {
        inode->flags = FS_DIRECTORY;
        inode->f_ops = &ramfs_dir_ops;
        inode->i_mode = ramfs_apply_umask(0777);
    } else {
        inode->flags = FS_FILE;
        inode->f_ops = &ramfs_file_ops;
        inode->i_size = 0;
        inode->i_mode = ramfs_apply_umask(0666);
    }

    return inode;
}

static int ramfs_unlink(struct dentry *dir_dentry, const char *name)
{
    if (!dir_dentry || !name)
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

    // Truncate the file mapping to free physical pages
    if (target->i_mapping)
        truncate_inode_pages(target->i_mapping, 0);

    target->flags = 0; // Mark as deleted/invalid
    target->i_size = 0;

    // Explicitly detach from parent dcache to ensure it disappears
    if (found->parent) {
        if (found->parent->children == found) {
            found->parent->children = found->sibling;
        } else {
            struct dentry *curr = found->parent->children;
            while (curr && curr->sibling != found)
                curr = curr->sibling;

            if (curr)
                curr->sibling = found->sibling;
        }
    }

    return 0;
}

static struct file_operations ramfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = generic_readdir,
    .finddir = NULL,
    .ioctl = NULL,
    .unlink = ramfs_unlink
};

static struct file_operations ramfs_file_ops = {
    .read = generic_file_read,
    .write = generic_file_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

struct super_block *ramfs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    (void)fs_type; (void)flags; (void)dev_name; (void)data;

    struct inode *inode = (struct inode*)kmalloc(sizeof(struct inode));
    if (!inode)
        panic("Failed to alloc ramfs root inode.");

    memset(inode, 0, sizeof(*inode));

    inode->flags = FS_DIRECTORY;
    inode->i_ino = 1;
    inode->f_ops = &ramfs_dir_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0755;

    struct address_space *mapping = (struct address_space*)kmalloc(sizeof(struct address_space));
    address_space_init(mapping, inode);
    inode->i_mapping = mapping;

    struct dentry *ramfs_dentry = d_alloc(NULL, "/");
    ramfs_dentry->inode = inode;

    // Create a generic super_block for the VFS
    struct super_block *sb = (struct super_block*)kmalloc(sizeof(struct super_block));
    sb->name = "ramfs";
    sb->root = inode;
    sb->fs_private = NULL;
    sb->refcount = 1;

    // To preserve existing behavior, we set the global vfs_root_dentry here if it's not set
    if (!vfs_root_dentry)
        vfs_root_dentry = ramfs_dentry;

    return sb;
}

static struct file_system_type ramfs_fs_type = {
    .name = "ramfs",
    .get_sb = ramfs_get_sb,
    .kill_sb = NULL,
    .next = NULL,
};

int init_ramfs_fs(void)
{
    static int initialized = 0;
    if (initialized)
        return 0;
    initialized = 1;

    register_filesystem(&ramfs_fs_type);
    printf("ramfs filesystem registered.\n");
    return 0;
}
