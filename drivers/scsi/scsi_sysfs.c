#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/list.h"

/*
 * Linux mapping:
 *   - linux2.6/drivers/scsi/scsi_sysfs.c: registers scsi_bus_type and sdev_class
 * Lite keeps a minimal bus + class registration to build /sys/bus/scsi and
 * /sys/class/scsi_device.
 */

struct bus_type scsi_bus_type = {
    .name = "scsi",
};

struct class sdev_class = {
    .name = "scsi_device",
};

static int scsi_sysfs_register(void)
{
    if (bus_register(&scsi_bus_type) != 0)
        return -1;

    if (class_register(&sdev_class) != 0)
        return -1;
    return 0;
}
subsys_initcall(scsi_sysfs_register);
