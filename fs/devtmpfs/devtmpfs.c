#include "linux/file.h"
#include "linux/devtmpfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/tty.h"
#include "linux/console.h"
#include "linux/kheap.h"
#include "linux/device_model.h"

#define MAX_DEVICES 32

static struct dirent dev_dirent;
static struct inode dev_console;
static struct inode dev_tty;
struct devtmpfs_node {
    char name[32];
    struct inode *inode;
};
static struct devtmpfs_node devtmpfs_nodes[MAX_DEVICES];
static uint32_t devtmpfs_node_count;
static int devtmpfs_ready;

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
    return tty_write(buffer, size);
}

static int dev_tty_ioctl(struct inode *node, uint32_t request, uint32_t arg)
{
    return dev_console_ioctl(node, request, arg);
}

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

static struct file_operations devtmpfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = devtmpfs_readdir,
    .finddir = devtmpfs_finddir,
    .ioctl = NULL
};

struct inode *devtmpfs_get_console(void)
{
    return &dev_console;
}

struct inode *devtmpfs_get_tty(void)
{
    return &dev_tty;
}

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

static void devtmpfs_add_for_device(struct device *dev)
{
    if (!dev || !dev->kobj.name[0])
        return;
    if (!strcmp(dev->kobj.name, "console"))
        devtmpfs_add_node("console", &dev_console);
}

void devtmpfs_register_device(struct device *dev)
{
    if (!devtmpfs_ready)
        return;
    devtmpfs_add_for_device(dev);
}

void devtmpfs_unregister_device(struct device *dev)
{
    if (!devtmpfs_ready || !dev)
        return;
    if (!strcmp(dev->kobj.name, "console"))
        devtmpfs_remove_node("console");
}

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
    dev_root->f_ops = &devtmpfs_dir_ops;
    dev_root->uid = 0;
    dev_root->gid = 0;
    dev_root->i_mode = 0555;

    memset(&dev_console, 0, sizeof(dev_console));
    dev_console.flags = FS_CHARDEVICE;
    dev_console.i_ino = 2;
    dev_console.i_size = 0;
    dev_console.f_ops = &dev_console_ops;
    dev_console.uid = 0;
    dev_console.gid = 0;
    dev_console.i_mode = 0666;

    memset(&dev_tty, 0, sizeof(dev_tty));
    dev_tty.flags = FS_CHARDEVICE;
    dev_tty.i_ino = 3;
    dev_tty.i_size = 0;
    dev_tty.f_ops = &dev_tty_ops;
    dev_tty.uid = 0;
    dev_tty.gid = 0;
    dev_tty.i_mode = 0666;

    devtmpfs_node_count = 0;
    devtmpfs_ready = 0;
    uint32_t count = device_model_device_count();
    for (uint32_t i = 0; i < count; i++) {
        struct device *dev = device_model_device_at(i);
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

static int init_devtmpfs_fs(void)
{
    register_filesystem(&devtmpfs_fs_type);
    printf("devtmpfs filesystem registered.\n");
    return 0;
}
fs_initcall(init_devtmpfs_fs);
