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
static struct gendisk ramdisk0_disk;
static struct gendisk ramdisk1_disk;

static struct device *ramdisk_virtual_root(void)
{
    struct device *vroot = virtual_root_device();
    if (!vroot)
        return NULL;
    struct device *child = virtual_child_device(vroot, "block");
    if (child)
        return child;
    struct device *dev = (struct device *)kmalloc(sizeof(*dev));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, "block");
    device_set_parent(dev, vroot);
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

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

static void ramdisk_bdev_cleanup(struct ramdisk_backend *backend, struct request_queue *q)
{
    if (q)
        blk_cleanup_queue(q);
    if (!backend)
        return;
    if (backend->data)
        vfree(backend->data);
    kfree(backend);
}

static int ramdisk_disk_init(struct gendisk *disk, const char *name, uint32_t minor,
                             uint32_t size, uint32_t block_size, struct device *parent)
{
    if (!disk || !name || size == 0)
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

    struct request_queue *q = blk_init_queue(ramdisk_request_fn, backend);
    if (!q) {
        ramdisk_bdev_cleanup(backend, NULL);
        return -1;
    }
    if (gendisk_init(disk, name, 1, minor) != 0) {
        ramdisk_bdev_cleanup(backend, q);
        return -1;
    }
    disk->queue = q;
    disk->block_size = block_size ? block_size : 512;
    disk->capacity = (uint64_t)size / 512ULL;
    disk->private_data = backend;
    disk->parent = parent;
    if (add_disk(disk) != 0) {
        ramdisk_bdev_cleanup(backend, q);
        return -1;
    }
    /* whole-disk bdev exists after registration; initialize simplified bdev fields */
    struct block_device *bdev = bdget_disk(disk, 0);
    if (bdev) {
        bdev->private_data = backend;
        bdev->block_size = disk->block_size;
        bdev->size = (uint64_t)size;
        bdput(bdev);
    }
    return 0;
}

static int ramdisk_init(void)
{
    struct device *parent = ramdisk_virtual_root();
    if (!parent)
        return -1;
    if (ramdisk_disk_init(&ramdisk0_disk, "ram0", 0, 8 * 1024 * 1024, 512, parent) != 0)
        return -1;
    if (ramdisk_disk_init(&ramdisk1_disk, "ram1", 1, 8 * 1024 * 1024, 512, parent) != 0)
        return -1;
    return 0;
}

module_init(ramdisk_init);
