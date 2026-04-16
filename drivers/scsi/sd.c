#include "scsi/scsi.h"
#include "scsi/scsi_host.h"
#include "linux/bio.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/slab.h"

static struct class sd_disk_class;

static int sd_disk_class_init(void)
{
    memset(&sd_disk_class, 0, sizeof(sd_disk_class));
    sd_disk_class.name = "scsi_disk";
    INIT_LIST_HEAD(&sd_disk_class.list);
    INIT_LIST_HEAD(&sd_disk_class.devices);
    return class_register(&sd_disk_class);
}
subsys_initcall(sd_disk_class_init);

static void scsi_disk_name(uint32_t index, char *name)
{
    if (!name)
        return;
    name[0] = 's';
    name[1] = 'd';
    name[2] = (char)('a' + (index % 26));
    name[3] = 0;
}

static void scsi_disk_request_fn(struct request_queue *q)
{
    if (!q)
        return;
    struct scsi_disk *sdkp = (struct scsi_disk *)q->queuedata;
    if (!sdkp || !sdkp->device) {
        while (blk_fetch_request(q))
            ;
        return;
    }

    struct request *rq;
    while ((rq = blk_fetch_request(q)) != NULL) {
        struct bio *bio = rq->bio;
        if (!bio || !bio->bi_bdev || !sdkp->disk) {
            blk_complete_request(q, rq, -1);
            continue;
        }
        struct block_device *bdev = bdget_disk(sdkp->disk, 0);
        if (!bdev) {
            blk_complete_request(q, rq, -1);
            continue;
        }
        uint32_t offset = (uint32_t)bio->bi_sector * 512u + bio->bi_byte_offset;
        if (offset + bio->bi_size > bdev->size || sdkp->device->sector_size == 0) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }
        if ((offset % sdkp->device->sector_size) != 0 || (bio->bi_size % sdkp->device->sector_size) != 0) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }
        uint32_t lba = offset / sdkp->device->sector_size;
        uint16_t blocks = (uint16_t)(bio->bi_size / sdkp->device->sector_size);
        int is_write = (bio->bi_opf == REQ_OP_WRITE);
        int ret = is_write ? scsi_write10(sdkp->device, lba, blocks, bio->bi_buf, bio->bi_size)
                           : scsi_read10(sdkp->device, lba, blocks, bio->bi_buf, bio->bi_size);
        if (ret != 0) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }
        block_account_io(bdev, is_write, bio->bi_size);
        bdput(bdev);
        blk_complete_request(q, rq, 0);
    }
}

int scsi_add_disk(struct scsi_disk *sdkp)
{
    if (!sdkp || !sdkp->device || !sdkp->disk)
        return -1;
    char disk_name[8];
    char tmp[12];

    sdkp->name[0] = 0;
    itoa((int)sdkp->device->host->host_no, 10, tmp);
    strcat(sdkp->name, tmp);
    strcat(sdkp->name, ":");
    itoa((int)sdkp->device->channel, 10, tmp);
    strcat(sdkp->name, tmp);
    strcat(sdkp->name, ":");
    itoa((int)sdkp->device->id, 10, tmp);
    strcat(sdkp->name, tmp);
    strcat(sdkp->name, ":");
    itoa((int)sdkp->device->lun, 10, tmp);
    strcat(sdkp->name, tmp);

    device_initialize(&sdkp->dev, sdkp->name);
    sdkp->dev.class = class_find("scsi_disk");
    device_set_parent(&sdkp->dev, &sdkp->device->sdev_gendev);
    if (device_add(&sdkp->dev) != 0)
        return -1;

    scsi_disk_name(sdkp->index, disk_name);
    if (gendisk_init(sdkp->disk, disk_name, 8, sdkp->index * 16) != 0)
        return -1;
    sdkp->disk->queue = blk_init_queue(scsi_disk_request_fn, sdkp);
    if (!sdkp->disk->queue)
        return -1;
    sdkp->disk->capacity = sdkp->device->capacity_sectors;
    sdkp->disk->block_size = sdkp->device->sector_size;
    sdkp->disk->private_data = sdkp;
    sdkp->disk->parent = &sdkp->dev;
    if (add_disk(sdkp->disk) != 0)
        return -1;
    struct block_device *bdev = bdget_disk(sdkp->disk, 0);
    if (bdev) {
        bdev->size = sdkp->disk->capacity * 512ULL;
        bdev->block_size = sdkp->device->sector_size;
        bdev->private_data = sdkp;
        bdput(bdev);
    }
    return 0;
}
