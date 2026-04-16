#include "linux/file.h"
#include "linux/devtmpfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/tty.h"
#include "linux/console.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/blkdev.h"
#include "base.h"

#define MAX_DEVICES 32

static struct dirent dev_dirent;
static struct inode dev_console;
static struct inode dev_tty;
static struct inode_operations devtmpfs_dir_iops;
struct devtmpfs_node {
    char name[32];
    struct inode *inode;
};
static struct devtmpfs_node devtmpfs_nodes[MAX_DEVICES];
static uint32_t devtmpfs_node_count;
static int devtmpfs_ready;

static int devtmpfs_console_ioctl(uint32_t request, uint32_t arg)
{
    if (request == CONSOLE_IOCTL_GETFLAGS)
        return (int)tty_get_flags();
    if (request == CONSOLE_IOCTL_SETFLAGS) {
        tty_set_flags(arg);
        return 0;
    }
    return -1;
}

static uint32_t devtmpfs_chr_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)offset;
    if (!buffer || size == 0)
        return 0;
    if (!node)
        return 0;
    dev_t devt = (dev_t)node->impl;
    if (devt == MKDEV(5, 1) || devt == MKDEV(5, 0) || MAJOR(devt) == 4)
        return tty_read_blocking((char *)buffer, size);
    return 0;
}

static uint32_t devtmpfs_chr_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)offset;
    if (!buffer || size == 0)
        return 0;
    if (!node)
        return 0;
    dev_t devt = (dev_t)node->impl;
    if (devt == MKDEV(5, 1))
        return console_write(buffer, size);
    if (devt == MKDEV(5, 0) || MAJOR(devt) == 4)
        return tty_write(buffer, size);
    return 0;
}

static int devtmpfs_chr_ioctl(struct inode *node, uint32_t request, uint32_t arg)
{
    (void)node;
    return devtmpfs_console_ioctl(request, arg);
}

/* devtmpfs_readdir: Implement devtmpfs readdir. */
static struct dirent *devtmpfs_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index >= devtmpfs_node_count)
        return NULL;
    strcpy(dev_dirent.name, devtmpfs_nodes[index].name);
    dev_dirent.ino = devtmpfs_nodes[index].inode ? devtmpfs_nodes[index].inode->i_ino : 0;
    return &dev_dirent;
}

/* devtmpfs_finddir: Implement devtmpfs finddir. */
static struct inode *devtmpfs_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    for (uint32_t i = 0; i < devtmpfs_node_count; i++) {
        if (!strcmp(name, devtmpfs_nodes[i].name))
            return devtmpfs_nodes[i].inode;
    }
    return NULL;
}

static struct file_operations devtmpfs_chr_ops = {
    .read = devtmpfs_chr_read,
    .write = devtmpfs_chr_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = devtmpfs_chr_ioctl
};

static struct file_operations devtmpfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = devtmpfs_readdir,
    .ioctl = NULL
};

static struct inode_operations devtmpfs_dir_iops = {
    .lookup = devtmpfs_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

/* devtmpfs_get_console: Implement devtmpfs get console. */
struct inode *devtmpfs_get_console(void)
{
    return &dev_console;
}

/* devtmpfs_get_tty: Implement devtmpfs get TTY. */
struct inode *devtmpfs_get_tty(void)
{
    return &dev_tty;
}

/* devtmpfs_add_node: Implement devtmpfs add node. */
static int devtmpfs_add_node(const char *name, struct inode *inode)
{
    if (!name || !*name || !inode)
        return -1;
    if (devtmpfs_node_count >= MAX_DEVICES)
        return -1;
    for (uint32_t i = 0; i < devtmpfs_node_count; i++) {
        if (!strcmp(devtmpfs_nodes[i].name, name))
            return 0;
    }
    uint32_t n = (uint32_t)strlen(name);
    if (n >= sizeof(devtmpfs_nodes[devtmpfs_node_count].name))
        n = sizeof(devtmpfs_nodes[devtmpfs_node_count].name) - 1;
    memcpy(devtmpfs_nodes[devtmpfs_node_count].name, name, n);
    devtmpfs_nodes[devtmpfs_node_count].name[n] = 0;
    devtmpfs_nodes[devtmpfs_node_count].inode = inode;
    devtmpfs_node_count++;
    return 0;
}

/* devtmpfs_remove_node: Implement devtmpfs remove node. */
static int devtmpfs_remove_node(const char *name)
{
    if (!name || !*name)
        return -1;
    for (uint32_t i = 0; i < devtmpfs_node_count; i++) {
        if (!strcmp(devtmpfs_nodes[i].name, name)) {
            for (uint32_t j = i + 1; j < devtmpfs_node_count; j++)
                devtmpfs_nodes[j - 1] = devtmpfs_nodes[j];
            devtmpfs_node_count--;
            return 0;
        }
    }
    return -1;
}

/* devtmpfs_add_for_device: Implement devtmpfs add for device. */
static void devtmpfs_add_for_device(struct device *dev)
{
    const char *devnode;
    uint32_t mode = 0666;
    uint32_t uid = 0;
    uint32_t gid = 0;

    if (!dev)
        return;
    devnode = device_get_devnode(dev, &mode, &uid, &gid);
    if (!devnode || !devnode[0])
        return;

    if (dev->type == &disk_type) {
        struct gendisk *disk = gendisk_from_dev(dev);
        struct block_device *bdev = disk ? bdget_disk(disk, 0) : NULL;
        struct inode *inode = bdev ? blockdev_inode_create(bdev) : NULL;
        bdput(bdev);
        if (inode) {
            inode->i_mode = (mode & 0777);
            inode->uid = uid;
            inode->gid = gid;
            devtmpfs_add_node(devnode, inode);
        }
        return;
    }

    if (!dev->devt)
        return;

    struct inode *inode = (struct inode *)kmalloc(sizeof(*inode));
    if (!inode)
        return;
    memset(inode, 0, sizeof(*inode));
    inode->flags = FS_CHARDEVICE;
    inode->i_ino = 0x3000 + devtmpfs_node_count;
    inode->i_mode = (mode & 0777);
    inode->uid = uid;
    inode->gid = gid;
    inode->impl = (uintptr_t)dev->devt;
    inode->f_ops = &devtmpfs_chr_ops;
    devtmpfs_add_node(devnode, inode);
}

/* devtmpfs_register_device: Implement devtmpfs register device. */
void devtmpfs_register_device(struct device *dev)
{
    if (!devtmpfs_ready)
        return;
    devtmpfs_add_for_device(dev);
}

/* devtmpfs_unregister_device: Implement devtmpfs unregister device. */
void devtmpfs_unregister_device(struct device *dev)
{
    const char *devnode;

    if (!devtmpfs_ready || !dev)
        return;
    devnode = device_get_devnode(dev, NULL, NULL, NULL);
    if (devnode && devnode[0])
        devtmpfs_remove_node(devnode);
}

/* devtmpfs_fill_super: Implement devtmpfs fill super. */
static int devtmpfs_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    struct inode *dev_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!dev_root)
        return -1;

    memset(dev_root, 0, sizeof(struct inode));
    dev_root->flags = FS_DIRECTORY;
    dev_root->i_ino = 1;
    dev_root->i_op = &devtmpfs_dir_iops;
    dev_root->f_ops = &devtmpfs_dir_ops;
    dev_root->uid = 0;
    dev_root->gid = 0;
    dev_root->i_mode = 0555;

    memset(&dev_console, 0, sizeof(dev_console));
    dev_console.flags = FS_CHARDEVICE;
    dev_console.i_ino = 2;
    dev_console.i_size = 0;
    dev_console.f_ops = &devtmpfs_chr_ops;
    dev_console.uid = 0;
    dev_console.gid = 0;
    dev_console.i_mode = 0600;
    dev_console.impl = (uintptr_t)MKDEV(5, 1);

    memset(&dev_tty, 0, sizeof(dev_tty));
    dev_tty.flags = FS_CHARDEVICE;
    dev_tty.i_ino = 3;
    dev_tty.i_size = 0;
    dev_tty.f_ops = &devtmpfs_chr_ops;
    dev_tty.uid = 0;
    dev_tty.gid = 0;
    dev_tty.i_mode = 0666;
    dev_tty.impl = (uintptr_t)MKDEV(5, 0);

    devtmpfs_node_count = 0;
    devtmpfs_ready = 0;
    uint32_t count = registered_device_count();
    for (uint32_t i = 0; i < count; i++) {
        struct device *dev = registered_device_at(i);
        if (!dev)
            continue;
        devtmpfs_add_for_device(dev);
    }
    devtmpfs_add_node("tty", &dev_tty);
    devtmpfs_ready = 1;

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = dev_root;

    return 0;
}

static struct file_system_type devtmpfs_fs_type = {
    .name = "devtmpfs",
    .get_sb = vfs_get_sb_single,
    .fill_super = devtmpfs_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* init_devtmpfs_fs: Initialize devtmpfs fs. */
static int init_devtmpfs_fs(void)
{
    register_filesystem(&devtmpfs_fs_type);
    printf("devtmpfs filesystem registered.\n");
    return 0;
}
fs_initcall(init_devtmpfs_fs);
