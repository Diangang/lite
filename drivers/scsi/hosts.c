#include "linux/device.h"
#include "linux/init.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/list.h"

/*
 * Linux mapping:
 *   - linux2.6/drivers/scsi/hosts.c: registers shost_class ("scsi_host")
 * Lite keeps a minimal class registration to build /sys/class/scsi_host.
 */

struct class shost_class = {
    .name = "scsi_host",
};

static int scsi_init_hosts(void)
{
    return class_register(&shost_class);
}
subsys_initcall(scsi_init_hosts);
