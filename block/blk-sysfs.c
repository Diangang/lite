#include "linux/blkdev.h"
#include "linux/blk_queue.h"
#include "linux/device.h"
#include "linux/libc.h"
#include "linux/vsprintf.h"

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

static uint32_t queue_requests_show(struct device *dev, struct device_attribute *attr,
                                    char *buffer, uint32_t cap)
{
    (void)attr;
    struct gendisk *disk = gendisk_from_dev(dev);
    if (!disk || !disk->queue)
        return 0;
    return blk_sysfs_emit_u32_line(buffer, cap, disk->queue->nr_requests);
}

static uint32_t queue_requests_store(struct device *dev, struct device_attribute *attr,
                                     uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)attr;
    if (!dev || !buffer || offset || size == 0)
        return 0;
    struct gendisk *disk = gendisk_from_dev(dev);
    if (!disk || !disk->queue)
        return 0;

    /*
     * Linux mapping: linux2.6/block/blk-sysfs.c:queue_requests_store()
     * - /sys/class/block/<disk>/queue/nr_requests is writable
     * - enforces a minimum depth (BLKDEV_MIN_RQ)
     */
    unsigned int v = 0;
    unsigned int seen = 0;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = buffer[i];
        if (c >= '0' && c <= '9') {
            seen = 1;
            v = v * 10u + (unsigned int)(c - '0');
            continue;
        }
        break;
    }
    if (!seen)
        return 0;
    if (blk_update_nr_requests(disk->queue, v) != 0)
        return 0;
    return size;
}

static struct device_attribute queue_requests_attr = {
    .attr = { .name = "nr_requests", .mode = 0644 },
    .show = queue_requests_show,
    .store = queue_requests_store,
};

static const struct attribute *queue_attrs[] = {
    &queue_requests_attr.attr,
    NULL,
};

const struct attribute_group queue_attr_group = {
    .name = "queue",
    .attrs = queue_attrs,
    .is_visible = NULL,
};
