#include "linux/blkdev.h"
#include "linux/fs.h"
#include "linux/libc.h"
#include "linux/pagemap.h"
#include "linux/slab.h"
#include "linux/memlayout.h"

static int blkdev_readpage(struct inode *inode, uint32_t index, struct page_cache_entry *page)
{
    if (!inode || !page)
        return -1;
    if ((inode->flags & 0x7) != FS_BLOCKDEVICE)
        return -1;
    struct block_device *bdev = (struct block_device *)inode->private_data;
    if (!bdev)
        return -1;
    block_device_read(bdev, index * 4096, 4096,
                      (uint8_t *)memlayout_directmap_phys_to_virt(page->phys_addr));
    return 0;
}

static int blkdev_writepage(struct inode *inode, struct page_cache_entry *page)
{
    if (!inode || !page)
        return -1;
    if ((inode->flags & 0x7) != FS_BLOCKDEVICE)
        return -1;
    struct block_device *bdev = (struct block_device *)inode->private_data;
    if (!bdev)
        return -1;
    block_device_write(bdev, page->index * 4096, 4096,
                       (const uint8_t *)memlayout_directmap_phys_to_virt(page->phys_addr));
    return 0;
}

static struct address_space_operations def_blk_aops = {
    .readpage = blkdev_readpage,
    .writepage = blkdev_writepage
};

static void blkdev_open(struct inode *inode)
{
    if (!inode || (inode->flags & 0x7) != FS_BLOCKDEVICE)
        return;
    struct block_device *bdev = (struct block_device *)inode->private_data;
    (void)blkdev_get(bdev);
}

static void blkdev_release(struct inode *inode)
{
    if (!inode || (inode->flags & 0x7) != FS_BLOCKDEVICE)
        return;
    struct block_device *bdev = (struct block_device *)inode->private_data;
    blkdev_put(bdev);
}

static struct file_operations def_blk_fops = {
    .read = generic_file_read,
    .write = generic_file_write,
    .open = blkdev_open,
    .close = blkdev_release,
    .readdir = NULL,
    .ioctl = NULL
};

/* blockdev_inode_create: Implement blockdev inode create. */
struct inode *blockdev_inode_create(struct block_device *bdev)
{
    if (!bdev)
        return NULL;
    if (bdev->inode) {
        bdev->inode->i_size = (uint32_t)bdev->size;
        return bdev->inode;
    }
    /* Keep bdev alive as long as the inode exists (Linux: bd_inode lifetime). */
    bdgrab(bdev);
    struct inode *inode = alloc_special_inode(FS_BLOCKDEVICE, bdev->devt, &def_blk_fops,
                                              0666, 0, 0);
    if (!inode)
        return NULL;
    inode->i_size = bdev->size;
    inode->private_data = bdev;
    struct address_space *mapping = (struct address_space *)kmalloc(sizeof(struct address_space));
    if (!mapping) {
        kfree(inode);
        return NULL;
    }
    address_space_init(mapping, inode);
    mapping->a_ops = &def_blk_aops;
    inode->i_mapping = mapping;
    bdev->inode = inode;
    return inode;
}

void blockdev_inode_destroy(struct block_device *bdev)
{
    if (!bdev || !bdev->inode)
        return;
    struct inode *inode = bdev->inode;
    bdev->inode = NULL;
    if (inode->i_mapping) {
        /* Drop any cached pages to avoid leaking physical pages. */
        truncate_inode_pages(inode->i_mapping, 0);
        address_space_release(inode->i_mapping);
        kfree(inode->i_mapping);
        inode->i_mapping = NULL;
    }
    kfree(inode);
    bdput(bdev);
}
