/*
 * Lite block layer core helpers (single-queue, non-blk-mq).
 *
 * Linux mapping:
 * - gendisk / disk registration: block/genhd.c
 * - /sys/class/block devices: drivers/base + block layer integration
 *
 * Note: device core / sysfs integration in Lite is simplified.
 */
#include "linux/blkdev.h"
#include "linux/bio.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/slab.h"
#include "linux/libc.h"

static uint32_t blk_reads;
static uint32_t blk_writes;
static uint32_t blk_bytes_read;
static uint32_t blk_bytes_written;
static struct class block_class;

static uint32_t blk_sysfs_emit_u32_line(char *buffer, uint32_t cap, uint32_t value)
{
    if (!buffer || cap < 2)
        return 0;
    itoa((int)value, 10, buffer);
    uint32_t n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return 0;
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static uint32_t block_attr_show_size(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    struct gendisk *disk = gendisk_from_dev(dev);
    uint32_t value = 0;
    if (disk && disk->bdev && disk->bdev->block_size)
        value = disk->bdev->size / disk->bdev->block_size;
    return blk_sysfs_emit_u32_line(buffer, cap, value);
}

static struct device_attribute block_attr_size = {
    .attr = { .name = "size", .mode = 0444 },
    .show = block_attr_show_size,
};

static const struct attribute *block_device_attrs[] = {
    &block_attr_size.attr,
    NULL,
};

static const struct attribute_group block_device_group = {
    .name = NULL,
    .attrs = block_device_attrs,
    .is_visible = NULL,
};

static const struct attribute_group *block_device_groups[] = {
    &block_device_group,
    NULL,
};

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
    struct class *cls = class_find("block");
    if (!cls)
        return NULL;
    struct device *dev = (struct device *)kmalloc(sizeof(*dev));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, disk->disk_name);
    dev->type = "block";
    dev->class = cls;
    dev->groups = block_device_groups;
    dev->driver_data = disk;
    if (parent)
        device_set_parent(dev, parent);
    dev->dev_major = disk->major;
    dev->dev_minor = disk->minor;
    dev->devnode_name = disk->disk_name;
    if (device_add(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
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
