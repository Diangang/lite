#include "linux/blkdev.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/slab.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"

/*
 * Linux mapping: linux2.6/block/genhd.c
 *
 * Lite keeps a minimal subset:
 * - gendisk object init
 * - disk registration into driver-core (class "block")
 * - sysfs attrs for /sys/class/block/<disk>/{size,stat} and queue/ (via blk-sysfs)
 *
 * No partitions yet; each gendisk maps to a whole-disk devt.
 */

static struct class block_class;

static uint32_t blk_sysfs_emit_u32_line(char *buffer, uint32_t cap, uint32_t value)
{
    if (!buffer || cap < 2)
        return 0;
    snprintf(buffer, cap, "%u", value);
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
    snprintf(tmp, sizeof(tmp), "%u", v);
    return blk_sysfs_append(buffer, off, cap, tmp);
}

static uint32_t block_attr_show_size(struct device *dev, struct device_attribute *attr,
                                     char *buffer, uint32_t cap)
{
    (void)attr;
    struct gendisk *disk = gendisk_from_dev(dev);
    uint32_t value = 0;
    /* Linux /sys/class/block/<disk>/size is in 512-byte sectors. */
    if (disk)
        value = (uint32_t)(disk->capacity > 0xFFFFFFFFu ? 0xFFFFFFFFu : disk->capacity);
    return blk_sysfs_emit_u32_line(buffer, cap, value);
}

/* Forward decl in fs/block_dev.c (bdev accounting). */
struct block_device *bdget(uint32_t devt);
void bdput(struct block_device *bdev);

static uint32_t block_attr_show_stat(struct device *dev, struct device_attribute *attr,
                                     char *buffer, uint32_t cap)
{
    (void)attr;
    if (!buffer || cap < 2)
        return 0;

    /*
     * Linux mapping: /sys/class/block/<disk>/stat (see linux2.6/block/genhd.c +
     * block/blk-sysfs.c).
     *
     * Lite provides a minimal subset based on bdev accounting. We keep the
     * 11-field Linux shape, but most fields are 0 because we do not implement
     * merges, in-flight timing, or queue time accounting yet.
     */
    uint32_t rd = 0, wr = 0, rsec = 0, wsec = 0, in_flight = 0;
    if (dev && dev->devt) {
        struct block_device *bdev = bdget((uint32_t)dev->devt);
        if (bdev) {
            rd = bdev->reads;
            wr = bdev->writes;
            rsec = bdev->bytes_read / 512u;
            wsec = bdev->bytes_written / 512u;
            if (bdev->disk && bdev->disk->queue)
                in_flight = bdev->disk->queue->in_flight;
            bdput(bdev);
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

static struct device_attribute block_attr_size = {
    .attr = { .name = "size", .mode = 0444 },
    .show = block_attr_show_size,
};

static struct device_attribute block_attr_stat = {
    .attr = { .name = "stat", .mode = 0444 },
    .show = block_attr_show_stat,
};

static const struct attribute *disk_attrs[] = {
    &block_attr_size.attr,
    &block_attr_stat.attr,
    NULL,
};

static const struct attribute_group disk_attr_group = {
    .name = NULL,
    .attrs = disk_attrs,
    .is_visible = NULL,
};

/*
 * Provided by block/blk-sysfs.c (Linux mapping: queue sysfs attrs).
 */
extern const struct attribute_group queue_attr_group;

static const struct attribute_group *disk_attr_groups[] = {
    &disk_attr_group,
    &queue_attr_group,
    NULL,
};

static const char *block_devnode(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid)
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
    .devnode = block_devnode,
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

/* Whole-disk gendisk registry keyed by devt (no partitions yet). */
struct disk_map_entry {
    uint32_t devt;
    struct gendisk *disk;
};

static struct disk_map_entry disk_map[32];
static uint32_t disk_map_count;

static struct gendisk *disk_lookup(uint32_t devt)
{
    for (uint32_t i = 0; i < disk_map_count; i++) {
        if (disk_map[i].devt == devt)
            return disk_map[i].disk;
    }
    return NULL;
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

struct gendisk *get_gendisk(uint32_t devt)
{
    return disk_lookup(devt);
}

int add_disk(struct gendisk *disk)
{
    if (!disk)
        return -1;
    if (!block_register_disk(disk, disk->parent))
        return -1;
    return 0;
}

struct device *block_register_disk(struct gendisk *disk, struct device *parent)
{
    if (!disk)
        return NULL;
    struct class *cls = &block_class;

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
    struct block_device *bdev = bdget(devt);
    if (bdev && bdev->openers) {
        printf("block: refusing to delete busy gendisk %s (openers=%u)\n",
               disk->disk_name, bdev->openers);
        bdput(bdev);
        return -1;
    }
    if (bdev)
        bdput(bdev);

    if (disk->dev) {
        device_unregister(disk->dev);
        disk->dev = NULL;
    }
    disk_unregister(devt);
    (void)bd_remove(devt);
    return 0;
}

void put_disk(struct gendisk *disk)
{
    if (!disk)
        return;
    kfree(disk);
}

struct gendisk *gendisk_from_dev(struct device *dev)
{
    if (!dev || dev->type != &disk_type)
        return NULL;
    return (struct gendisk *)dev->driver_data;
}

static int genhd_device_init(void)
{
    memset(&block_class, 0, sizeof(block_class));
    block_class.name = "block";
    INIT_LIST_HEAD(&block_class.list);
    INIT_LIST_HEAD(&block_class.devices);
    block_class.dev_groups = disk_attr_groups;
    return class_register(&block_class);
}
subsys_initcall(genhd_device_init);
