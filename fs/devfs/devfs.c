#include "file.h"
#include "devfs.h"
#include "libc.h"
#include "init.h"
#include "tty.h"
#include "console.h"
#include "kheap.h"
#include "device_model.h"

#define MAX_DEVICES 32

static struct dirent dev_dirent;
// Note: dev_root is dynamically allocated in init_devfs now
static struct inode dev_console;
static struct inode dev_tty;

static uint32_t dev_console_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0)
        return 0;
    return tty_read_blocking((char*)buffer, size);
}

static uint32_t dev_console_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)node;
    (void)offset;
    return console_write(buffer, size);
}

static int dev_console_ioctl(struct inode *node, uint32_t request, uint32_t arg)
{
    (void)node;
    if (request == CONSOLE_IOCTL_GETFLAGS)
        return (int)tty_get_flags();
    if (request == CONSOLE_IOCTL_SETFLAGS) {
        tty_set_flags(arg);
        return 0;
    }
    return -1;
}

static uint32_t dev_tty_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0)
        return 0;
    return tty_read_blocking((char*)buffer, size);
}

static uint32_t dev_tty_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)node;
    (void)offset;
    return console_write(buffer, size);
}

static int dev_tty_ioctl(struct inode *node, uint32_t request, uint32_t arg)
{
    return dev_console_ioctl(node, request, arg);
}

static struct dirent *dev_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(dev_dirent.name, "console");
        dev_dirent.ino = dev_console.i_ino;
        return &dev_dirent;
    }
    if (index == 1) {
        strcpy(dev_dirent.name, "tty");
        dev_dirent.ino = dev_tty.i_ino;
        return &dev_dirent;
    }
    return NULL;
}

static struct inode *dev_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "console"))
        return &dev_console;
    if (!strcmp(name, "tty"))
        return &dev_tty;
    return NULL;
}

static struct file_operations dev_console_ops = {
    .read = dev_console_read,
    .write = dev_console_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = dev_console_ioctl
};

static struct file_operations dev_tty_ops = {
    .read = dev_tty_read,
    .write = dev_tty_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = dev_tty_ioctl
};

static struct file_operations devfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = dev_readdir,
    .finddir = dev_finddir,
    .ioctl = NULL
};

struct inode *devfs_get_console(void)
{
    return &dev_console;
}

void init_devfs(void)
{
    struct inode *dev_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!dev_root)
        return;

    memset(dev_root, 0, sizeof(struct inode));
    dev_root->flags = FS_DIRECTORY;
    dev_root->f_ops = &devfs_dir_ops;
    dev_root->uid = 0;
    dev_root->gid = 0;
    dev_root->i_mode = 0555;

    memset(&dev_console, 0, sizeof(dev_console));
    dev_console.flags = FS_CHARDEVICE;
    dev_console.i_ino = 1;
    dev_console.i_size = 0;
    dev_console.f_ops = &dev_console_ops;
    dev_console.uid = 0;
    dev_console.gid = 0;
    dev_console.i_mode = 0666;

    memset(&dev_tty, 0, sizeof(dev_tty));
    dev_tty.flags = FS_CHARDEVICE;
    dev_tty.i_ino = 2;
    dev_tty.i_size = 0;
    dev_tty.f_ops = &dev_tty_ops;
    dev_tty.uid = 0;
    dev_tty.gid = 0;
    dev_tty.i_mode = 0666;

    struct dentry *dev_dentry = d_alloc(NULL, "/dev");
    dev_dentry->inode = dev_root;
    vfs_mount_root("/dev", dev_dentry);
}

struct super_block *devfs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    (void)fs_type; (void)flags; (void)dev_name; (void)data;
    return NULL;
}

static struct file_system_type devfs_fs_type = {
    .name = "devfs",
    .get_sb = devfs_get_sb,
    .kill_sb = NULL,
    .next = NULL,
};

static int init_devfs_fs(void)
{
    register_filesystem(&devfs_fs_type);
    printf("devfs filesystem registered.\n");
    return 0;
}
module_init(init_devfs_fs);
