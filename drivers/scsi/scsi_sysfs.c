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

struct bus_type scsi_bus_type;

static struct class sdev_class;

static int scsi_sysfs_register(void)
{
    memset(&scsi_bus_type, 0, sizeof(scsi_bus_type));
    scsi_bus_type.name = "scsi";
    INIT_LIST_HEAD(&scsi_bus_type.list);
    if (bus_register_static(&scsi_bus_type) != 0)
        return -1;

    memset(&sdev_class, 0, sizeof(sdev_class));
    sdev_class.name = "scsi_device";
    INIT_LIST_HEAD(&sdev_class.list);
    INIT_LIST_HEAD(&sdev_class.devices);
    if (class_register(&sdev_class) != 0)
        return -1;
    return 0;
}
subsys_initcall(scsi_sysfs_register);
