#include "linux/chrdev.h"
#include "linux/fs.h"
#include "linux/slab.h"
#include "linux/tty.h"
#include "linux/console.h"

static int chrdev_ioctl(uint32_t request, uint32_t arg)
{
    if (request == CONSOLE_IOCTL_GETFLAGS)
        return (int)tty_get_flags();
    if (request == CONSOLE_IOCTL_SETFLAGS) {
        tty_set_flags(arg);
        return 0;
    }
    return -1;
}

static uint32_t chrdev_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    dev_t devt;

    (void)offset;
    if (!buffer || size == 0 || !node)
        return 0;
    devt = (dev_t)node->impl;
    if (devt == MKDEV(5, 1) || devt == MKDEV(5, 0) || MAJOR(devt) == 4)
        return tty_read_blocking((char *)buffer, size);
    return 0;
}

static uint32_t chrdev_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    dev_t devt;

    (void)offset;
    if (!buffer || size == 0 || !node)
        return 0;
    devt = (dev_t)node->impl;
    if (devt == MKDEV(5, 1))
        return console_write(buffer, size);
    if (devt == MKDEV(5, 0) || MAJOR(devt) == 4)
        return tty_write(buffer, size);
    return 0;
}

static int chrdev_inode_ioctl(struct inode *node, uint32_t request, uint32_t arg)
{
    (void)node;
    return chrdev_ioctl(request, arg);
}

static struct file_operations chrdev_file_ops = {
    .read = chrdev_read,
    .write = chrdev_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = chrdev_inode_ioctl,
};

struct inode *chrdev_inode_create(dev_t devt, uint32_t mode, uint32_t uid, uint32_t gid)
{
    return alloc_special_inode(FS_CHARDEVICE, devt, &chrdev_file_ops, mode, uid, gid);
}

void chrdev_inode_destroy(struct inode *inode)
{
    if (!inode)
        return;
    kfree(inode);
}
