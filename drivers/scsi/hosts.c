#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/list.h"

/*
 * Linux mapping:
 *   - linux2.6/drivers/scsi/hosts.c: registers shost_class ("scsi_host")
 * Lite keeps a minimal class registration to build /sys/class/scsi_host.
 */

static struct class shost_class;

static int scsi_init_hosts(void)
{
    memset(&shost_class, 0, sizeof(shost_class));
    shost_class.name = "scsi_host";
    INIT_LIST_HEAD(&shost_class.list);
    INIT_LIST_HEAD(&shost_class.devices);
    return class_register(&shost_class);
}
subsys_initcall(scsi_init_hosts);
