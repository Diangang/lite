#include "linux/blkdev.h"
#include "linux/bio.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/slab.h"
#include "linux/vmalloc.h"
#include "linux/libc.h"

/* bio_endio_sync: Implement bio endio sync. */
static uint32_t blk_reads;
static uint32_t blk_writes;
static uint32_t blk_bytes_read;
static uint32_t blk_bytes_written;
static struct class block_class;

static int block_class_init(void)
{
    memset(&block_class, 0, sizeof(block_class));
    kobject_init(&block_class.kobj, "block", NULL);
    INIT_LIST_HEAD(&block_class.list);
    INIT_LIST_HEAD(&block_class.devices);
    return class_register(&block_class);
}
core_initcall(block_class_init);

int gendisk_init(struct gendisk *disk, const char *name, uint32_t major, uint32_t minor, struct block_device *bdev)
{
    if (!disk || !name || !bdev)
        return -1;
    memset(disk, 0, sizeof(*disk));
    uint32_t len = (uint32_t)strlen(name);
    if (len >= sizeof(disk->disk_name))
        len = sizeof(disk->disk_name) - 1;
    memcpy(disk->disk_name, name, len);
    disk->disk_name[len] = 0;
    disk->major = major;
    disk->minor = minor;
    disk->bdev = bdev;
    disk->dev = NULL;
    disk->parent = NULL;
    bdev->disk = disk;
    return 0;
}

struct device *block_register_disk(struct gendisk *disk, struct device *parent)
{
    if (!disk || !disk->bdev)
        return NULL;
    struct bus_type *bus = device_model_platform_bus();
    struct class *cls = device_model_block_class();
    if (!bus || !cls)
        return NULL;
    struct device *dev = device_register_simple_class_parent(disk->disk_name, "block", bus, cls, parent, disk);
    if (!dev)
        return NULL;
    disk->dev = dev;
    disk->parent = parent;
    return dev;
}

struct gendisk *gendisk_from_dev(struct device *dev)
{
    if (!dev || !dev->type || strcmp(dev->type, "block"))
        return NULL;
    return (struct gendisk *)dev->driver_data;
}

static void bio_endio_sync(struct bio *bio, int error)
{
    if (!bio)
        return;
    bio->bi_status = error;
}

/* mem_bdev_request_fn: Process requests against the in-memory block device backend. */
static void mem_bdev_request_fn(struct request_queue *q)
{
    if (!q)
        return;
    while (1) {
        struct request *rq = blk_fetch_request(q);
        if (!rq)
            break;
        struct bio *bio = rq->bio;
        if (!bio || !bio->bi_bdev || !bio->bi_buf || bio->bi_size == 0) {
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
            memcpy(bio->bi_buf, bdev->data + offset, bio->bi_size);
            bdev->reads++;
            bdev->bytes_read += bio->bi_size;
            blk_reads++;
            blk_bytes_read += bio->bi_size;
            blk_complete_request(q, rq, 0);
        } else if (bio->bi_opf == REQ_OP_WRITE) {
            memcpy(bdev->data + offset, bio->bi_buf, bio->bi_size);
            bdev->writes++;
            bdev->bytes_written += bio->bi_size;
            blk_writes++;
            blk_bytes_written += bio->bi_size;
            blk_complete_request(q, rq, 0);
        } else {
            blk_complete_request(q, rq, -1);
        }
    }
}

/* block_device_init: Initialize block device. */
int block_device_init(struct block_device *bdev, uint32_t size, uint32_t block_size)
{
    if (!bdev || size == 0)
        return -1;
    uint8_t *data = (uint8_t *)vmalloc(size);
    if (!data)
        return -1;
    memset(data, 0, size);
    bdev->size = size;
    bdev->block_size = block_size ? block_size : 512;
    bdev->data = data;
    bdev->queue = NULL;
    bdev->reads = 0;
    bdev->writes = 0;
    bdev->bytes_read = 0;
    bdev->bytes_written = 0;
    bdev->disk = NULL;

    struct request_queue *q = (struct request_queue *)kmalloc(sizeof(struct request_queue));
    if (!q) {
        vfree(data);
        bdev->data = NULL;
        return -1;
    }
    q->make_request_fn = NULL;
    q->request_fn = mem_bdev_request_fn;
    q->head = NULL;
    q->tail = NULL;
    q->queuedata = bdev;
    bdev->queue = q;
    return 0;
}

/* block_device_read: Implement block device read. */
uint32_t block_device_read(struct block_device *bdev, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!bdev || !buffer || size == 0)
        return 0;
    if (offset >= bdev->size)
        return 0;
    if (offset + size > bdev->size)
        size = bdev->size - offset;
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

/* block_device_write: Implement block device write. */
uint32_t block_device_write(struct block_device *bdev, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!bdev || !buffer || size == 0)
        return 0;
    if (offset >= bdev->size)
        return 0;
    if (offset + size > bdev->size)
        size = bdev->size - offset;
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

/* get_block_stats: Get block stats. */
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
