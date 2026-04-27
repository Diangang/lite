#include "linux/device.h"
#include "linux/init.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/list.h"
#include "linux/slab.h"
#include "scsi/scsi_host.h"

/*
 * Linux mapping:
 *   - linux2.6/drivers/scsi/hosts.c: registers shost_class ("scsi_host")
 * Lite keeps a minimal class registration to build /sys/class/scsi_host.
 */

struct class shost_class = {
    .name = "scsi_host",
};

/* Linux mapping: linux2.6/drivers/scsi/hosts.c::scsi_host_next_hn */
static uint32_t scsi_host_next_hn;

struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *sht, void *hostdata)
{
    struct Scsi_Host *shost;

    if (!sht || !sht->queuecommand)
        return NULL;
    shost = (struct Scsi_Host *)kmalloc(sizeof(*shost));
    if (!shost)
        return NULL;
    memset(shost, 0, sizeof(*shost));
    shost->hostt = sht;
    shost->hostdata = hostdata;
    shost->host_no = scsi_host_next_hn++;
    shost->max_channel = 0;
    shost->max_id = 1;
    shost->max_lun = 1;
    return shost;
}

int scsi_init_hosts(void)
{
    return class_register(&shost_class);
}
