#include "linux/blkdev.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"

/* ramdisk_init: Initialize ramdisk. */
static struct block_device ramdisk0_bdev;
static struct block_device ramdisk1_bdev;
static struct gendisk ramdisk0_disk;
static struct gendisk ramdisk1_disk;

static int ramdisk_init(void)
{
    struct device *parent = device_model_platform_root();
    if (!parent)
        return -1;
    if (block_device_init(&ramdisk0_bdev, 8 * 1024 * 1024, 512) != 0)
        return -1;
    if (gendisk_init(&ramdisk0_disk, "ram0", 1, 0, &ramdisk0_bdev) != 0)
        return -1;
    if (!block_register_disk(&ramdisk0_disk, parent))
        return -1;
    if (block_device_init(&ramdisk1_bdev, 8 * 1024 * 1024, 512) != 0)
        return -1;
    if (gendisk_init(&ramdisk1_disk, "ram1", 1, 1, &ramdisk1_bdev) != 0)
        return -1;
    if (!block_register_disk(&ramdisk1_disk, parent))
        return -1;
    return 0;
}

module_init(ramdisk_init);
