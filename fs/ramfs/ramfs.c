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
    if (!name || !*name) return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '/') return 0;
    }
    return 1;
}

struct inode *ramfs_create_child(struct inode *dir, const char *name, uint32_t type)
{
    if (!dir || !name) return NULL;
    if (!ramfs_valid_name(name)) return NULL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return NULL;
    // Actually, `ramfs_create_child` is called by `vfs_open` and `vfs_mkdir` when it wants to create a file.
    // They already handle the dcache! So `ramfs_create_child` just needs to return a new bare inode!

    struct inode *inode = (struct inode*)kmalloc(sizeof(struct inode));
    if (!inode) return NULL;
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

static struct file_operations ramfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = generic_readdir,
    .finddir = NULL,
    .ioctl = NULL
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

void init_ramfs(void)
{
    struct inode *inode = (struct inode*)kmalloc(sizeof(struct inode));
    if (!inode)
        panic("Failed to alloc ramfs inode.");

    memset(inode, 0, sizeof(*inode));

    inode->flags = FS_DIRECTORY;
    inode->i_ino = 1; // The root of the whole filesystem is typically 1 or 2
    inode->f_ops = &ramfs_dir_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0755;

    struct address_space *mapping = (struct address_space*)kmalloc(sizeof(struct address_space));
    address_space_init(mapping, inode);
    inode->i_mapping = mapping;

    vfs_mount_root("/", inode);
}
