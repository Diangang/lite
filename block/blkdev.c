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

static int blk_sysfs_append(char *buffer, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buffer || !off || !s)
        return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (*off + n >= cap)
        return 0;
    memcpy(buffer + *off, s, n);
    *off += n;
    buffer[*off] = 0;
    return 1;
}

static int blk_sysfs_append_ch(char *buffer, uint32_t *off, uint32_t cap, char ch)
{
    if (!buffer || !off)
        return 0;
    if (*off + 1 >= cap)
        return 0;
    buffer[*off] = ch;
    *off += 1;
    buffer[*off] = 0;
    return 1;
}

static int blk_sysfs_append_u32(char *buffer, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[16];
    itoa((int)v, 10, tmp);
    return blk_sysfs_append(buffer, off, cap, tmp);
}

static uint32_t block_attr_show_size(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    struct gendisk *disk = gendisk_from_dev(dev);
    uint32_t value = 0;
    /* Linux /sys/class/block/<disk>/size is in 512-byte sectors. */
    if (disk)
        value = (uint32_t)(disk->capacity > 0xFFFFFFFFu ? 0xFFFFFFFFu : disk->capacity);
    return blk_sysfs_emit_u32_line(buffer, cap, value);
}

/* Forward decl: used by sysfs stat show before bdev_map helpers. */
static struct block_device *bdev_lookup(uint32_t devt);

static uint32_t block_attr_show_stat(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    if (!buffer || cap < 2)
        return 0;

    /*
     * Linux mapping: /sys/class/block/<disk>/stat (see linux2.6/block/genhd.c + block/blk-sysfs.c).
     *
     * Lite provides a minimal subset based on bdev accounting. We keep the
     * 11-field Linux shape, but most fields are 0 because we do not implement
     * merges, in-flight tracking, or timing in the block layer yet.
     */
    uint32_t rd = 0, wr = 0, rsec = 0, wsec = 0, in_flight = 0;
    if (dev && dev->devt) {
        struct block_device *bdev = bdev_lookup((uint32_t)dev->devt);
        if (bdev) {
            rd = bdev->reads;
            wr = bdev->writes;
            rsec = bdev->bytes_read / 512u;
            wsec = bdev->bytes_written / 512u;
            if (bdev->disk && bdev->disk->queue)
                in_flight = bdev->disk->queue->in_flight;
        }
    }

    uint32_t off = 0;
    buffer[0] = 0;
    /* reads merges sectors read_ms writes merges sectors write_ms in_flight io_ms wio_ms */
    if (!blk_sysfs_append_u32(buffer, &off, cap, rd) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, 0) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, rsec) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, 0) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, wr) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, 0) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, wsec) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, in_flight) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, 0) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, 0) ||
        !blk_sysfs_append_ch(buffer, &off, cap, ' ') ||
        !blk_sysfs_append_u32(buffer, &off, cap, 0) ||
        !blk_sysfs_append_ch(buffer, &off, cap, '\n')) {
        return 0;
    }
    return off;
}

static uint32_t block_attr_show_queue_nr_requests(struct device *dev, struct device_attribute *attr,
                                                  char *buffer, uint32_t cap)
{
    (void)attr;
    struct gendisk *disk = gendisk_from_dev(dev);
    if (!disk || !disk->queue)
        return 0;
    return blk_sysfs_emit_u32_line(buffer, cap, disk->queue->nr_requests);
}

static struct device_attribute block_attr_size = {
    .attr = { .name = "size", .mode = 0444 },
    .show = block_attr_show_size,
};

static struct device_attribute block_attr_stat = {
    .attr = { .name = "stat", .mode = 0444 },
    .show = block_attr_show_stat,
};

static struct device_attribute block_attr_queue_nr_requests = {
    .attr = { .name = "nr_requests", .mode = 0444 },
    .show = block_attr_show_queue_nr_requests,
};

static const struct attribute *block_device_attrs[] = {
    &block_attr_size.attr,
    &block_attr_stat.attr,
    NULL,
};

static const struct attribute_group block_device_group = {
    .name = NULL,
    .attrs = block_device_attrs,
    .is_visible = NULL,
};

static const struct attribute *block_queue_attrs[] = {
    &block_attr_queue_nr_requests.attr,
    NULL,
};

static const struct attribute_group block_queue_group = {
    .name = "queue",
    .attrs = block_queue_attrs,
    .is_visible = NULL,
};

static const struct attribute_group *block_device_groups[] = {
    &block_device_group,
    &block_queue_group,
    NULL,
};

static const char *disk_devnode(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid)
{
    if (mode)
        *mode = 0666;
    if (uid)
        *uid = 0;
    if (gid)
        *gid = 0;
    return dev ? dev->kobj.name : NULL;
}

const struct device_type disk_type = {
    .name = "disk",
    .devnode = disk_devnode,
};

int gendisk_init(struct gendisk *disk, const char *name, uint32_t major, uint32_t first_minor)
{
    if (!disk || !name)
        return -1;
    memset(disk, 0, sizeof(*disk));
    uint32_t len = (uint32_t)strlen(name);
    if (len >= sizeof(disk->disk_name))
        len = sizeof(disk->disk_name) - 1;
    memcpy(disk->disk_name, name, len);
    disk->disk_name[len] = 0;
    disk->major = major;
    disk->first_minor = first_minor;
    disk->queue = NULL;
    disk->capacity = 0;
    disk->block_size = 512;
    disk->private_data = NULL;
    disk->dev = NULL;
    disk->parent = NULL;
    return 0;
}

/* Simple whole-disk bdev registry keyed by devt (no partitions yet). */
struct bdev_map_entry {
    uint32_t devt;
    struct block_device *bdev;
};

static struct bdev_map_entry bdev_map[32];
static uint32_t bdev_map_count;

/* Whole-disk gendisk registry keyed by devt (no partitions yet). */
struct disk_map_entry {
    uint32_t devt;
    struct gendisk *disk;
};

static struct disk_map_entry disk_map[32];
static uint32_t disk_map_count;

static struct block_device *bdev_lookup(uint32_t devt)
{
    for (uint32_t i = 0; i < bdev_map_count; i++) {
        if (bdev_map[i].devt == devt)
            return bdev_map[i].bdev;
    }
    return NULL;
}

static struct gendisk *disk_lookup(uint32_t devt)
{
    for (uint32_t i = 0; i < disk_map_count; i++) {
        if (disk_map[i].devt == devt)
            return disk_map[i].disk;
    }
    return NULL;
}

static int bdev_register(uint32_t devt, struct block_device *bdev)
{
    if (!bdev || bdev_map_count >= (sizeof(bdev_map) / sizeof(bdev_map[0])))
        return -1;
    if (bdev_lookup(devt))
        return 0;
    bdev_map[bdev_map_count].devt = devt;
    bdev_map[bdev_map_count].bdev = bdev;
    bdev_map_count++;
    return 0;
}

static int bdev_unregister(uint32_t devt)
{
    for (uint32_t i = 0; i < bdev_map_count; i++) {
        if (bdev_map[i].devt == devt) {
            for (uint32_t j = i + 1; j < bdev_map_count; j++)
                bdev_map[j - 1] = bdev_map[j];
            bdev_map_count--;
            return 0;
        }
    }
    return -1;
}

static int disk_register(uint32_t devt, struct gendisk *disk)
{
    if (!disk || disk_map_count >= (sizeof(disk_map) / sizeof(disk_map[0])))
        return -1;
    if (disk_lookup(devt))
        return 0;
    disk_map[disk_map_count].devt = devt;
    disk_map[disk_map_count].disk = disk;
    disk_map_count++;
    return 0;
}

static int disk_unregister(uint32_t devt)
{
    for (uint32_t i = 0; i < disk_map_count; i++) {
        if (disk_map[i].devt == devt) {
            for (uint32_t j = i + 1; j < disk_map_count; j++)
                disk_map[j - 1] = disk_map[j];
            disk_map_count--;
            return 0;
        }
    }
    return -1;
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
    /* Default to gendisk->private_data (Linux: gendisk->private_data). */
    bdev->private_data = disk->private_data;
    /* One reference is held by the bdev_map registry while registered. */
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
    /*
     * At refcnt==0 the bdev must have been removed from the registry and must
     * not have an inode pinned.
     */
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
    struct gendisk *disk = disk_lookup(devt);
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

int add_disk(struct gendisk *disk)
{
    if (!disk)
        return -1;
    /* Minimal Linux-like wrapper: register the disk device and keep bdev reachable by dev_t. */
    if (!block_register_disk(disk, disk->parent))
        return -1;
    return 0;
}

struct device *block_register_disk(struct gendisk *disk, struct device *parent)
{
    if (!disk)
        return NULL;
    struct class *cls = class_find("block");
    if (!cls)
        return NULL;

    struct device *dev = (struct device *)kmalloc(sizeof(*dev));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, disk->disk_name);
    dev->type = &disk_type;
    dev->class = cls;
    dev->driver_data = disk;
    if (parent)
        device_set_parent(dev, parent);
    dev->devt = MKDEV(disk->major, disk->first_minor);
    if (disk_register(dev->devt, disk) != 0) {
        kfree(dev);
        return NULL;
    }
    if (device_add(dev) != 0) {
        disk_unregister(dev->devt);
        kobject_put(&dev->kobj);
        return NULL;
    }
    disk->dev = dev;
    disk->parent = parent;
    return dev;
}

int del_gendisk(struct gendisk *disk)
{
    if (!disk)
        return -1;
    uint32_t devt = MKDEV(disk->major, disk->first_minor);
    struct block_device *bdev = bdev_lookup(devt);
    if (bdev && bdev->openers) {
        printf("block: refusing to delete busy gendisk %s (openers=%u)\n",
               disk->disk_name, bdev->openers);
        return -1;
    }

    if (disk->dev) {
        device_unregister(disk->dev);
        disk->dev = NULL;
    }
    disk_unregister(devt);

    if (bdev) {
        blockdev_inode_destroy(bdev);
        bdev_unregister(devt);
        bdput(bdev);
    }
    return 0;
}

struct gendisk *gendisk_from_dev(struct device *dev)
{
    if (!dev || dev->type != &disk_type)
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

static int block_class_init(void)
{
    memset(&block_class, 0, sizeof(block_class));
    block_class.name = "block";
    INIT_LIST_HEAD(&block_class.list);
    INIT_LIST_HEAD(&block_class.devices);
    block_class.dev_groups = block_device_groups;
    return class_register(&block_class);
}
core_initcall(block_class_init);
