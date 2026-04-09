#include "linux/blkdev.h"
#include "linux/fs.h"
#include "linux/libc.h"
#include "linux/pagemap.h"
#include "linux/slab.h"

static struct file_operations blockdev_ops = {
    .read = generic_file_read,
    .write = generic_file_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

/* blockdev_inode_create: Implement blockdev inode create. */
struct inode *blockdev_inode_create(struct block_device *bdev)
{
    if (!bdev)
        return NULL;
    struct inode *inode = (struct inode *)kmalloc(sizeof(struct inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    inode->flags = FS_BLOCKDEVICE;
    inode->i_ino = get_next_ino();
    inode->f_ops = &blockdev_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0666;
    inode->i_size = bdev->size;
    inode->private_data = bdev;
    struct address_space *mapping = (struct address_space *)kmalloc(sizeof(struct address_space));
    if (!mapping) {
        kfree(inode);
        return NULL;
    }
    address_space_init(mapping, inode);
    inode->i_mapping = mapping;
    return inode;
}
