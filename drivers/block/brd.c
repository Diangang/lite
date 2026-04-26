#include "linux/blkdev.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/blkdev.h"
#include "linux/blkdev.h"
#include "linux/bio.h"
#include "linux/vmalloc.h"
#include "linux/slab.h"
#include "base.h"

struct ramdisk_backend {
    uint8_t *data;
};

struct brd_device {
    int brd_number;
    struct request_queue *brd_queue;
    struct gendisk *brd_disk;
    struct list_head brd_list;
    struct ramdisk_backend *brd_backend;
};

static LIST_HEAD(brd_devices);

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
    struct brd_device *brd = (struct brd_device *)q->queuedata;
    struct ramdisk_backend *backend = brd ? brd->brd_backend : NULL;
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

static struct brd_device *brd_alloc(int i, uint32_t size, uint32_t block_size, struct device *parent)
{
    char name[16];
    struct brd_device *brd;
    struct gendisk *disk;
    struct ramdisk_backend *backend = (struct ramdisk_backend *)kmalloc(sizeof(*backend));

    if (!backend || size == 0)
        goto out_free_backend;
    brd = (struct brd_device *)kmalloc(sizeof(*brd));
    if (!brd)
        goto out_free_backend;
    memset(brd, 0, sizeof(*brd));
    INIT_LIST_HEAD(&brd->brd_list);
    brd->brd_number = i;
    memset(backend, 0, sizeof(*backend));
    backend->data = (uint8_t *)vmalloc(size);
    if (!backend->data) {
        goto out_free_brd;
    }
    memset(backend->data, 0, size);

    struct request_queue *q = blk_init_queue(ramdisk_request_fn, brd);
    if (!q) {
        goto out_free_data;
    }
    disk = (struct gendisk *)kmalloc(sizeof(*disk));
    if (!disk) {
        ramdisk_bdev_cleanup(backend, q);
        goto out_free_brd;
    }
    snprintf(name, sizeof(name), "ram%d", i);
    if (gendisk_init(disk, name, 1, (uint32_t)i) != 0) {
        kfree(disk);
        ramdisk_bdev_cleanup(backend, q);
        goto out_free_brd;
    }
    brd->brd_queue = q;
    brd->brd_disk = disk;
    brd->brd_backend = backend;
    disk->queue = q;
    disk->block_size = block_size ? block_size : 512;
    disk->capacity = (uint64_t)size / 512ULL;
    disk->private_data = brd;
    disk->parent = parent;
    return brd;

out_free_data:
    if (backend->data)
        vfree(backend->data);
out_free_brd:
    kfree(brd);
out_free_backend:
    if (backend)
        kfree(backend);
    return NULL;
}

static void brd_free(struct brd_device *brd)
{
    struct block_device *bdev;

    if (!brd)
        return;
    if (brd->brd_disk)
        del_gendisk(brd->brd_disk);
    bdev = brd->brd_disk ? bdget_disk(brd->brd_disk, 0) : NULL;
    if (bdev) {
        bdev->private_data = NULL;
        bdput(bdev);
    }
    if (brd->brd_disk)
        put_disk(brd->brd_disk);
    ramdisk_bdev_cleanup(brd->brd_backend, brd->brd_queue);
    kfree(brd);
}

static int brd_init_one(int i, uint32_t size, uint32_t block_size, struct device *parent)
{
    struct brd_device *brd = brd_alloc(i, size, block_size, parent);
    struct block_device *bdev;

    if (!brd)
        return -1;
    if (add_disk(brd->brd_disk) != 0) {
        brd_free(brd);
        return -1;
    }
    bdev = bdget_disk(brd->brd_disk, 0);
    if (bdev) {
        bdev->private_data = brd->brd_backend;
        bdev->block_size = brd->brd_disk->block_size;
        bdev->size = (uint64_t)size;
        bdput(bdev);
    }
    list_add_tail(&brd->brd_list, &brd_devices);
    return 0;
}

/* brd_init: Initialize block ramdisks. */
static int brd_init(void)
{
    struct brd_device *brd, *next;
    struct device *parent = ramdisk_virtual_root();

    if (!parent)
        return -1;
    if (brd_init_one(0, 8 * 1024 * 1024, 512, parent) != 0)
        goto out_free;
    if (brd_init_one(1, 8 * 1024 * 1024, 512, parent) != 0)
        goto out_free;
    return 0;

out_free:
    list_for_each_entry_safe(brd, next, &brd_devices, brd_list) {
        list_del(&brd->brd_list);
        brd_free(brd);
    }
    return -1;
}

module_init(brd_init);
