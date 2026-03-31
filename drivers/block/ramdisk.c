#include "linux/blkdev.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"

static struct block_device ramdisk_bdev;

static int ramdisk_init(void)
{
    struct bus_type *bus = device_model_platform_bus();
    if (!bus)
        return -1;
    if (block_device_init(&ramdisk_bdev, 8 * 1024 * 1024, 512) != 0)
        return -1;
    if (!device_register_simple("ram0", "block", bus, &ramdisk_bdev))
        return -1;
    return 0;
}

module_init(ramdisk_init);
