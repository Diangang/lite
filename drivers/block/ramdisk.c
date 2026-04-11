#include "linux/blkdev.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/bio.h"
#include "linux/vmalloc.h"
#include "linux/slab.h"
#include "base.h"

struct ramdisk_backend {
    uint8_t *data;
};

/* ramdisk_init: Initialize ramdisk. */
static struct block_device ramdisk0_bdev;
static struct block_device ramdisk1_bdev;
static struct gendisk ramdisk0_disk;
static struct gendisk ramdisk1_disk;

static void ramdisk_request_fn(struct request_queue *q)
{
    if (!q)
        return;
    struct ramdisk_backend *backend = (struct ramdisk_backend *)q->queuedata;
    while (1) {
        struct request *rq = blk_fetch_request(q);
        if (!rq)
            break;
        struct bio *bio = rq->bio;
        if (!bio || !bio->bi_bdev || !backend || !backend->data || !bio->bi_buf || bio->bi_size == 0) {
            blk_complete_request(q, rq, -1);
            continue;
        }
        struct block_device *bdev = bio->bi_bdev;
        uint32_t offset = (uint32_t)bio->bi_sector * 512 + bio->bi_byte_offset;
        if (offset >= bdev->size || offset + bio->bi_size > bdev->size) {
            blk_complete_request(q, rq, -1);
            continue;
        }
        if (bio->bi_opf == REQ_OP_READ) {
            memcpy(bio->bi_buf, backend->data + offset, bio->bi_size);
            block_account_io(bdev, 0, bio->bi_size);
            blk_complete_request(q, rq, 0);
        } else if (bio->bi_opf == REQ_OP_WRITE) {
            memcpy(backend->data + offset, bio->bi_buf, bio->bi_size);
            block_account_io(bdev, 1, bio->bi_size);
            blk_complete_request(q, rq, 0);
        } else {
            blk_complete_request(q, rq, -1);
        }
    }
}

static int ramdisk_bdev_init(struct block_device *bdev, uint32_t size, uint32_t block_size)
{
    if (!bdev || size == 0)
        return -1;
    struct ramdisk_backend *backend = (struct ramdisk_backend *)kmalloc(sizeof(*backend));
    if (!backend)
        return -1;
    memset(backend, 0, sizeof(*backend));
    backend->data = (uint8_t *)vmalloc(size);
    if (!backend->data) {
        kfree(backend);
        return -1;
    }
    memset(backend->data, 0, size);

    memset(bdev, 0, sizeof(*bdev));
    bdev->size = size;
    bdev->block_size = block_size ? block_size : 512;
    bdev->private_data = backend;
    bdev->queue = blk_init_queue(ramdisk_request_fn, backend);
    if (!bdev->queue) {
        vfree(backend->data);
        kfree(backend);
        bdev->private_data = NULL;
        return -1;
    }
    return 0;
}

static int ramdisk_init(void)
{
    struct device *parent = device_model_virtual_subsys("block");
    if (!parent)
        return -1;
    if (ramdisk_bdev_init(&ramdisk0_bdev, 8 * 1024 * 1024, 512) != 0)
        return -1;
    if (gendisk_init(&ramdisk0_disk, "ram0", 1, 0, &ramdisk0_bdev) != 0)
        return -1;
    if (!block_register_disk(&ramdisk0_disk, parent))
        return -1;
    if (ramdisk_bdev_init(&ramdisk1_bdev, 8 * 1024 * 1024, 512) != 0)
        return -1;
    if (gendisk_init(&ramdisk1_disk, "ram1", 1, 1, &ramdisk1_bdev) != 0)
        return -1;
    if (!block_register_disk(&ramdisk1_disk, parent))
        return -1;
    return 0;
}

module_init(ramdisk_init);
