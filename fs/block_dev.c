#include "linux/blkdev.h"
#include "linux/bio.h"
#include "linux/fs.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/pagemap.h"
#include "linux/slab.h"
#include "asm/pgtable.h"

static uint32_t blk_reads;
static uint32_t blk_writes;
static uint32_t blk_bytes_read;
static uint32_t blk_bytes_written;

int bd_remove(uint32_t devt)
{
    struct block_device *bdev = bdev_lookup(devt);
    if (!bdev)
        return 0;
    if (bdev->openers)
        return -1;

    if (bdev->inode)
        blockdev_inode_destroy(bdev);

    (void)bdev_unregister(devt);
    bdput(bdev);
    return 0;
}

static struct block_device *bdev_alloc_for_disk(struct gendisk *disk)
{
    if (!disk)
        return NULL;
    struct block_device *bdev = (struct block_device *)kmalloc(sizeof(*bdev));
    if (!bdev)
        return NULL;
    memset(bdev, 0, sizeof(*bdev));
    bdev->devt = MKDEV(disk->major, disk->first_minor);
    bdev->disk = disk;
    bdev->block_size = disk->block_size ? disk->block_size : 512;
    bdev->size = disk->capacity * 512ULL;
    bdev->private_data = disk->private_data;
    bdev->refcnt = 1;
    bdev->openers = 0;
    bdev->inode = NULL;
    if (bdev_register(bdev->devt, bdev) != 0) {
        kfree(bdev);
        return NULL;
    }
    return bdev;
}

struct block_device *bdgrab(struct block_device *bdev)
{
    if (!bdev)
        return NULL;
    bdev->refcnt++;
    return bdev;
}

void bdput(struct block_device *bdev)
{
    if (!bdev || bdev->refcnt == 0)
        return;
    bdev->refcnt--;
    if (bdev->refcnt != 0)
        return;
    if (bdev->openers || bdev->inode) {
        printf("block: bdput refcnt underflow guard devt=%x openers=%u inode=%p\n",
               bdev->devt, bdev->openers, bdev->inode);
        return;
    }
    kfree(bdev);
}

struct block_device *bdget(uint32_t devt)
{
    struct block_device *bdev = bdev_lookup(devt);
    if (bdev)
        return bdgrab(bdev);
    struct gendisk *disk = get_gendisk(devt);
    if (!disk)
        return NULL;
    bdev = bdev_alloc_for_disk(disk);
    return bdev ? bdgrab(bdev) : NULL;
}

struct block_device *bdget_disk(struct gendisk *disk, int index)
{
    if (!disk || index != 0)
        return NULL;
    uint32_t devt = MKDEV(disk->major, disk->first_minor);
    struct block_device *bdev = bdev_lookup(devt);
    if (bdev)
        return bdgrab(bdev);
    bdev = bdev_alloc_for_disk(disk);
    return bdev ? bdgrab(bdev) : NULL;
}

int blkdev_get(struct block_device *bdev)
{
    if (!bdev)
        return -1;
    bdev->openers++;
    return 0;
}

void blkdev_put(struct block_device *bdev)
{
    if (!bdev || bdev->openers == 0)
        return;
    bdev->openers--;
}

static void bio_endio_sync(struct bio *bio, int error)
{
    if (!bio)
        return;
    bio->bi_status = error;
}

uint32_t block_device_read(struct block_device *bdev, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!bdev || !buffer || size == 0)
        return 0;
    if (offset >= bdev->size)
        return 0;
    if (size > bdev->size - offset)
        size = (uint32_t)(bdev->size - offset);
    struct bio bio;
    memset(&bio, 0, sizeof(bio));
    bio.bi_bdev = bdev;
    bio.bi_sector = offset / 512;
    bio.bi_byte_offset = offset % 512;
    bio.bi_size = size;
    bio.bi_buf = buffer;
    bio.bi_opf = REQ_OP_READ;
    bio.bi_end_io = bio_endio_sync;
    bio.bi_status = 0;
    if (submit_bio(&bio) != 0 || bio.bi_status != 0)
        return 0;
    return size;
}

uint32_t block_device_write(struct block_device *bdev, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!bdev || !buffer || size == 0)
        return 0;
    if (offset >= bdev->size)
        return 0;
    if (size > bdev->size - offset)
        size = (uint32_t)(bdev->size - offset);
    struct bio bio;
    memset(&bio, 0, sizeof(bio));
    bio.bi_bdev = bdev;
    bio.bi_sector = offset / 512;
    bio.bi_byte_offset = offset % 512;
    bio.bi_size = size;
    bio.bi_buf = (uint8_t *)buffer;
    bio.bi_opf = REQ_OP_WRITE;
    bio.bi_end_io = bio_endio_sync;
    bio.bi_status = 0;
    if (submit_bio(&bio) != 0 || bio.bi_status != 0)
        return 0;
    return size;
}

void block_account_io(struct block_device *bdev, int is_write, uint32_t bytes)
{
    if (!bdev || bytes == 0)
        return;
    if (is_write) {
        bdev->writes++;
        bdev->bytes_written += bytes;
        blk_writes++;
        blk_bytes_written += bytes;
    } else {
        bdev->reads++;
        bdev->bytes_read += bytes;
        blk_reads++;
        blk_bytes_read += bytes;
    }
}

void get_block_stats(uint32_t *reads, uint32_t *writes, uint32_t *bytes_read, uint32_t *bytes_written)
{
    if (reads)
        *reads = blk_reads;
    if (writes)
        *writes = blk_writes;
    if (bytes_read)
        *bytes_read = blk_bytes_read;
    if (bytes_written)
        *bytes_written = blk_bytes_written;
}

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
    if (!inode) {
        bdput(bdev);
        return NULL;
    }
    inode->i_size = bdev->size;
    inode->private_data = bdev;
    struct address_space *mapping = (struct address_space *)kmalloc(sizeof(struct address_space));
    if (!mapping) {
        kfree(inode);
        bdput(bdev);
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
