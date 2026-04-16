#include "scsi/scsi.h"
#include "scsi/scsi_host.h"
#include "linux/init.h"
#include "linux/blk_queue.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/timer.h"
#include "linux/time.h"

static uint32_t scsi_host_next;
static uint32_t scsi_disk_next;
static const uint32_t scsi_scan_tur_retries = 3;
static const uint32_t scsi_report_luns_initial = 511;
static const uint32_t scsi_sequential_scan_max_luns = 8;

struct scsi_lun {
    uint8_t scsi_lun[8];
};

static int scsi_probe_and_add_lun(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun);
static struct scsi_target *scsi_alloc_or_lookup_target(struct Scsi_Host *shost, uint32_t channel, uint32_t id);
static void scsi_target_reap(struct Scsi_Host *shost, struct scsi_target *starget);
static void scsi_sync_done(struct scsi_cmnd *cmd);

static void scsi_copy_trim(char *dst, uint32_t dlen, const uint8_t *src, uint32_t slen)
{
    uint32_t n = slen < dlen - 1 ? slen : dlen - 1;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = (char)src[i];
    dst[n] = 0;
    while (n > 0 && dst[n - 1] == ' ')
        dst[--n] = 0;
}

struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *sht, void *hostdata)
{
    if (!sht || !sht->queuecommand)
        return NULL;
    struct Scsi_Host *shost = (struct Scsi_Host *)kmalloc(sizeof(*shost));
    if (!shost)
        return NULL;
    memset(shost, 0, sizeof(*shost));
    shost->hostt = sht;
    shost->hostdata = hostdata;
    shost->host_no = scsi_host_next++;
    shost->max_channel = 0;
    shost->max_id = 1;
    shost->max_lun = 1;
    return shost;
}

int scsi_add_host(struct Scsi_Host *shost, struct device *parent)
{
    if (!shost)
        return -1;
    strcpy(shost->name, "host");
    char num[12];
    itoa((int)shost->host_no, 10, num);
    strcat(shost->name, num);
    device_initialize(&shost->shost_gendev, shost->name);
    shost->shost_gendev.class = class_find("scsi_host");
    shost->shost_gendev.bus = &scsi_bus_type;
    if (parent)
        device_set_parent(&shost->shost_gendev, parent);
    return device_add(&shost->shost_gendev);
}

struct scsi_device *scsi_alloc_device(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun)
{
    if (!shost)
        return NULL;
    struct scsi_device *sdev = (struct scsi_device *)kmalloc(sizeof(*sdev));
    if (!sdev)
        return NULL;
    memset(sdev, 0, sizeof(*sdev));
    sdev->host = shost;
    sdev->channel = channel;
    sdev->id = id;
    sdev->lun = lun;
    sdev->sector_size = 512;
    return sdev;
}

int scsi_add_device(struct scsi_device *sdev)
{
    if (!sdev || !sdev->host)
        return -1;
    struct scsi_target *starget = scsi_alloc_or_lookup_target(sdev->host, sdev->channel, sdev->id);
    if (!starget)
        return -1;
    sdev->sdev_target = starget;

    char tmp[12];
    sdev->name[0] = 0;
    itoa((int)sdev->host->host_no, 10, tmp);
    strcat(sdev->name, tmp);
    strcat(sdev->name, ":");
    itoa((int)sdev->channel, 10, tmp);
    strcat(sdev->name, tmp);
    strcat(sdev->name, ":");
    itoa((int)sdev->id, 10, tmp);
    strcat(sdev->name, tmp);
    strcat(sdev->name, ":");
    itoa((int)sdev->lun, 10, tmp);
    strcat(sdev->name, tmp);
    device_initialize(&sdev->sdev_gendev, sdev->name);
    sdev->sdev_gendev.class = class_find("scsi_device");
    sdev->sdev_gendev.bus = &scsi_bus_type;
    device_set_parent(&sdev->sdev_gendev, &starget->dev);
    if (device_add(&sdev->sdev_gendev) != 0) {
        scsi_target_reap(sdev->host, starget);
        sdev->sdev_target = NULL;
        return -1;
    }
    return 0;
}

static void scsi_release_disk(struct scsi_disk *sdkp)
{
    if (!sdkp)
        return;
    if (sdkp->disk) {
        if (sdkp->disk->dev)
            del_gendisk(sdkp->disk);
        if (sdkp->disk->queue)
            blk_cleanup_queue(sdkp->disk->queue);
        kfree(sdkp->disk);
    }
    device_unregister(&sdkp->dev);
    kfree(sdkp);
}

static void scsi_release_device(struct scsi_device *sdev)
{
    if (!sdev)
        return;
    device_unregister(&sdev->sdev_gendev);
    kfree(sdev);
}

static void scsi_release_target(struct scsi_target *starget)
{
    if (!starget)
        return;
    if (starget->host && starget->host->hostt && starget->host->hostt->target_destroy)
        starget->host->hostt->target_destroy(starget);
    device_unregister(&starget->dev);
    kfree(starget);
}

static struct scsi_target *scsi_lookup_target(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    if (!shost)
        return NULL;
    for (uint32_t i = 0; i < shost->nr_targets; i++) {
        struct scsi_target *starget = shost->stargets[i];
        if (!starget)
            continue;
        if (starget->channel == channel && starget->id == id)
            return starget;
    }
    return NULL;
}

static int scsi_target_has_devices(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    if (!shost)
        return 0;
    for (uint32_t i = 0; i < shost->nr_devices; i++) {
        struct scsi_device *sdev = shost->sdevs[i];
        if (!sdev)
            continue;
        if (sdev->channel == channel && sdev->id == id)
            return 1;
    }
    return 0;
}

static struct scsi_target *scsi_alloc_target(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    if (!shost || shost->nr_targets >= SCSI_HOST_MAX_DEVICES)
        return NULL;

    struct scsi_target *starget = (struct scsi_target *)kmalloc(sizeof(*starget));
    if (!starget)
        return NULL;
    memset(starget, 0, sizeof(*starget));
    starget->host = shost;
    starget->channel = channel;
    starget->id = id;

    char tmp[12];
    strcpy(starget->name, "target");
    itoa((int)shost->host_no, 10, tmp);
    strcat(starget->name, tmp);
    strcat(starget->name, ":");
    itoa((int)channel, 10, tmp);
    strcat(starget->name, tmp);
    strcat(starget->name, ":");
    itoa((int)id, 10, tmp);
    strcat(starget->name, tmp);

    device_initialize(&starget->dev, starget->name);
    starget->dev.bus = &scsi_bus_type;
    device_set_parent(&starget->dev, &shost->shost_gendev);
    if (device_add(&starget->dev) != 0) {
        kfree(starget);
        return NULL;
    }
    if (shost->hostt && shost->hostt->target_alloc) {
        if (shost->hostt->target_alloc(starget) != 0) {
            device_unregister(&starget->dev);
            kfree(starget);
            return NULL;
        }
    }

    shost->stargets[shost->nr_targets++] = starget;
    return starget;
}

static struct scsi_target *scsi_alloc_or_lookup_target(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    struct scsi_target *starget = scsi_lookup_target(shost, channel, id);
    if (starget)
        return starget;
    return scsi_alloc_target(shost, channel, id);
}

static void scsi_target_reap(struct Scsi_Host *shost, struct scsi_target *starget)
{
    if (!shost || !starget)
        return;
    if (scsi_target_has_devices(shost, starget->channel, starget->id))
        return;

    for (uint32_t i = 0; i < shost->nr_targets; i++) {
        if (shost->stargets[i] != starget)
            continue;
        for (uint32_t j = i + 1; j < shost->nr_targets; j++)
            shost->stargets[j - 1] = shost->stargets[j];
        shost->nr_targets--;
        shost->stargets[shost->nr_targets] = NULL;
        scsi_release_target(starget);
        return;
    }
}

static int scsi_store_host_entry(struct Scsi_Host *shost, struct scsi_device *sdev, struct scsi_disk *sdkp)
{
    if (!shost || !sdev || !sdkp || shost->nr_devices >= SCSI_HOST_MAX_DEVICES)
        return -1;
    shost->sdevs[shost->nr_devices] = sdev;
    shost->sdks[shost->nr_devices] = sdkp;
    shost->nr_devices++;
    return 0;
}

static struct scsi_device *scsi_lookup_device(struct Scsi_Host *shost, uint32_t channel,
                                              uint32_t id, uint64_t lun)
{
    if (!shost)
        return NULL;
    for (uint32_t i = 0; i < shost->nr_devices; i++) {
        struct scsi_device *sdev = shost->sdevs[i];
        if (!sdev)
            continue;
        if (sdev->channel == channel && sdev->id == id && sdev->lun == lun)
            return sdev;
    }
    return NULL;
}

static uint32_t scsi_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

struct scsi_disk *scsi_alloc_disk(struct scsi_device *sdev)
{
    if (!sdev)
        return NULL;
    struct scsi_disk *sdkp = (struct scsi_disk *)kmalloc(sizeof(*sdkp));
    if (!sdkp)
        return NULL;
    memset(sdkp, 0, sizeof(*sdkp));
    sdkp->device = sdev;
    sdkp->index = scsi_disk_next++;
    sdkp->disk = (struct gendisk *)kmalloc(sizeof(*sdkp->disk));
    if (!sdkp->disk) {
        kfree(sdkp);
        return NULL;
    }
    memset(sdkp->disk, 0, sizeof(*sdkp->disk));
    return sdkp;
}

int scsi_execute_cmd(struct scsi_device *sdev, const uint8_t *cdb, uint32_t cdb_len,
                     void *data, uint32_t data_len, int dir,
                     uint8_t *sense, uint32_t *sense_len, uint8_t *status)
{
    if (!sdev || !sdev->host || !sdev->host->hostt || !sdev->host->hostt->queuecommand)
        return -1;

    struct scsi_cmnd sc;
    memset(&sc, 0, sizeof(sc));
    sc.device = sdev;
    sc.cmd_len = (unsigned short)(cdb_len > MAX_COMMAND_SIZE ? MAX_COMMAND_SIZE : cdb_len);
    memcpy(sc.cmnd, cdb, sc.cmd_len);

    sc.request_buffer = data;
    sc.request_bufflen = data_len;
    if (dir == SCSI_DATA_READ)
        sc.sc_data_direction = DMA_FROM_DEVICE;
    else if (dir == SCSI_DATA_WRITE)
        sc.sc_data_direction = DMA_TO_DEVICE;
    else
        sc.sc_data_direction = DMA_NONE;

    int done = 0;
    sc.scsi_done = scsi_sync_done;
    sc.host_scribble = &done;

    if (sdev->host->hostt->queuecommand(sdev->host, &sc) != 0)
        return -1;

    /*
     * Lite LLDDs complete synchronously today and must call scsi_done()
     * before returning. Do not busy-wait here: early boot may not have
     * a ticking clock yet.
     */
    if (!done)
        return -1;

    if (status)
        *status = scsi_status_byte(sc.result);
    if (sense && sense_len) {
        uint32_t n = sc.sense_len < *sense_len ? sc.sense_len : *sense_len;
        memcpy(sense, sc.sense_buffer, n);
        *sense_len = n;
    }
    return scsi_status_byte(sc.result) == SCSI_STATUS_GOOD ? 0 : -1;
}

static void scsi_sync_done(struct scsi_cmnd *cmd)
{
    if (!cmd || !cmd->host_scribble)
        return;
    int *done = (int *)cmd->host_scribble;
    *done = 1;
}

int scsi_test_unit_ready(struct scsi_device *sdev)
{
    uint8_t cdb[6];
    memset(cdb, 0, sizeof(cdb));
    uint8_t status = 0;
    return scsi_execute_cmd(sdev, cdb, sizeof(cdb), NULL, 0, SCSI_DATA_NONE, NULL, NULL, &status);
}

int scsi_inquiry(struct scsi_device *sdev, uint8_t *buf, uint32_t len)
{
    uint8_t cdb[6];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = INQUIRY;
    cdb[4] = (uint8_t)len;
    uint8_t status = 0;
    int ret = scsi_execute_cmd(sdev, cdb, sizeof(cdb), buf, len, SCSI_DATA_READ, NULL, NULL, &status);
    if (ret == 0 && buf && len >= 36) {
        sdev->type = buf[0] & 0x1F;
        scsi_copy_trim(sdev->vendor, sizeof(sdev->vendor), buf + 8, 8);
        scsi_copy_trim(sdev->model, sizeof(sdev->model), buf + 16, 16);
    }
    return ret;
}

int scsi_read_capacity(struct scsi_device *sdev, uint32_t *last_lba, uint32_t *block_size)
{
    uint8_t cdb[10];
    uint8_t buf[8];
    memset(cdb, 0, sizeof(cdb));
    memset(buf, 0, sizeof(buf));
    cdb[0] = READ_CAPACITY;
    uint8_t status = 0;
    int ret = scsi_execute_cmd(sdev, cdb, sizeof(cdb), buf, sizeof(buf), SCSI_DATA_READ, NULL, NULL, &status);
    if (ret != 0)
        return ret;
    uint32_t lba = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    uint32_t blksz = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 8) | (uint32_t)buf[7];
    if (last_lba)
        *last_lba = lba;
    if (block_size)
        *block_size = blksz;
    sdev->sector_size = blksz ? blksz : 512;
    sdev->capacity_sectors = ((uint64_t)lba + 1ULL) * ((uint64_t)sdev->sector_size / 512ULL);
    return 0;
}

int scsi_read10(struct scsi_device *sdev, uint32_t lba, uint16_t blocks, void *buf, uint32_t len)
{
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = READ_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)lba;
    cdb[7] = (uint8_t)(blocks >> 8);
    cdb[8] = (uint8_t)blocks;
    uint8_t status = 0;
    return scsi_execute_cmd(sdev, cdb, sizeof(cdb), buf, len, SCSI_DATA_READ, NULL, NULL, &status);
}

int scsi_write10(struct scsi_device *sdev, uint32_t lba, uint16_t blocks, const void *buf, uint32_t len)
{
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = WRITE_10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)lba;
    cdb[7] = (uint8_t)(blocks >> 8);
    cdb[8] = (uint8_t)blocks;
    uint8_t status = 0;
    return scsi_execute_cmd(sdev, cdb, sizeof(cdb), (void *)buf, len, SCSI_DATA_WRITE, NULL, NULL, &status);
}

static uint64_t scsilun_to_int(const struct scsi_lun *scsilun)
{
    uint64_t lun = 0;
    for (uint32_t i = 0; i < sizeof(lun); i += 2)
        lun |= (((uint64_t)scsilun->scsi_lun[i] << ((i + 1) * 8)) |
                ((uint64_t)scsilun->scsi_lun[i + 1] << (i * 8)));
    return lun;
}

static int scsi_sense_is_illegal_request(const uint8_t *sense, uint32_t sense_len,
                                         uint8_t *asc, uint8_t *ascq)
{
    if (!sense || sense_len < 14)
        return 0;
    uint8_t resp_code = sense[0] & 0x7F;
    if (resp_code != 0x70 && resp_code != 0x71)
        return 0;
    uint8_t key = sense[2] & 0x0F;
    if (key != 0x05)
        return 0;
    if (asc)
        *asc = sense[12];
    if (ascq)
        *ascq = sense[13];
    return 1;
}

static int scsi_report_lun_scan(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    if (!shost)
        return -1;
    if (!shost->max_lun)
        return -1;

    struct scsi_device *sdev = scsi_alloc_device(shost, channel, id, 0);
    if (!sdev)
        return -1;

    uint32_t alloc_len = (scsi_report_luns_initial + 1U) * sizeof(struct scsi_lun);
    uint8_t *buf = (uint8_t *)kmalloc(alloc_len);
    if (!buf) {
        kfree(sdev);
        return -1;
    }
    for (;;) {
        memset(buf, 0, alloc_len);

        uint8_t cdb[12];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = REPORT_LUNS;
        cdb[6] = (uint8_t)(alloc_len >> 24);
        cdb[7] = (uint8_t)(alloc_len >> 16);
        cdb[8] = (uint8_t)(alloc_len >> 8);
        cdb[9] = (uint8_t)alloc_len;

        int ret = -1;
        uint8_t status = 0;
        uint8_t sense[32];
        uint32_t sense_len = sizeof(sense);
        for (uint32_t attempt = 0; attempt < scsi_scan_tur_retries && ret != 0; attempt++)
            ret = scsi_execute_cmd(sdev, cdb, sizeof(cdb), buf, alloc_len, SCSI_DATA_READ,
                                   sense, &sense_len, &status);
        if (ret != 0) {
            /*
             * Linux caches "REPORT LUNS unsupported" on the target to avoid
             * repeated slow scans. We do the same for the common ILLEGAL
             * REQUEST cases.
             */
            if (status == SCSI_STATUS_CHECK_CONDITION) {
                uint8_t asc = 0, ascq = 0;
                if (scsi_sense_is_illegal_request(sense, sense_len, &asc, &ascq)) {
                    if (asc == 0x20 || asc == 0x24) { /* INVALID OPCODE / INVALID FIELD IN CDB */
                        struct scsi_target *starget = scsi_lookup_target(shost, channel, id);
                        if (starget)
                            starget->flags |= SCSI_TARGET_NO_REPORT_LUNS;
                    }
                }
            }
            kfree(buf);
            kfree(sdev);
            return -1;
        }

        uint32_t length = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                          ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
        if (length + sizeof(struct scsi_lun) > alloc_len) {
            uint64_t new_len64 = (uint64_t)length + sizeof(struct scsi_lun);
            if (new_len64 > 0xFFFFFFFFULL) {
                kfree(buf);
                kfree(sdev);
                return -1;
            }
            kfree(buf);
            alloc_len = (uint32_t)new_len64;
            buf = (uint8_t *)kmalloc(alloc_len);
            if (!buf) {
                kfree(sdev);
                return -1;
            }
            continue;
        }

        uint32_t num_luns = length / sizeof(struct scsi_lun);
        struct scsi_lun *luns = (struct scsi_lun *)(buf + sizeof(struct scsi_lun));
        int found = 0;
        for (uint32_t i = 0; i < num_luns; i++) {
            uint64_t lun = scsilun_to_int(&luns[i]);
            if (lun >= shost->max_lun)
                continue;
            if (scsi_probe_and_add_lun(shost, channel, id, lun) == 0)
                found = 1;
        }

        kfree(buf);
        kfree(sdev);
        return found ? 0 : -1;
    }
}

static int scsi_sequential_lun_scan(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    if (!shost || shost->max_lun <= 1)
        return 0;

    uint32_t max_lun = scsi_min_u32(shost->max_lun, scsi_sequential_scan_max_luns);
    for (uint32_t lun = 1; lun < max_lun; lun++) {
        if (scsi_probe_and_add_lun(shost, channel, id, lun) != 0)
            return 0;
    }
    return 0;
}

static int scsi_probe_and_add_lun(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun)
{
    if (scsi_lookup_device(shost, channel, id, lun))
        return 0;

    struct scsi_device *sdev = scsi_alloc_device(shost, channel, id, lun);
    if (!sdev)
        return -1;

    int ret = -1;
    for (uint32_t attempt = 0; attempt < scsi_scan_tur_retries && ret != 0; attempt++)
        ret = scsi_test_unit_ready(sdev);
    if (ret != 0) {
        kfree(sdev);
        return -1;
    }

    uint8_t inquiry[36];
    memset(inquiry, 0, sizeof(inquiry));
    ret = scsi_inquiry(sdev, inquiry, sizeof(inquiry));
    if (ret != 0 || sdev->type != TYPE_DISK) {
        kfree(sdev);
        return -1;
    }

    uint32_t last_lba = 0;
    uint32_t block_size = 512;
    ret = scsi_read_capacity(sdev, &last_lba, &block_size);
    if (ret != 0) {
        kfree(sdev);
        return -1;
    }

    ret = scsi_add_device(sdev);
    if (ret != 0) {
        scsi_target_reap(shost, sdev->sdev_target);
        kfree(sdev);
        return -1;
    }

    struct scsi_disk *sdkp = scsi_alloc_disk(sdev);
    if (!sdkp) {
        struct scsi_target *starget = sdev->sdev_target;
        scsi_release_device(sdev);
        scsi_target_reap(shost, starget);
        return -1;
    }
    ret = scsi_add_disk(sdkp);
    if (ret != 0) {
        struct scsi_target *starget = sdev->sdev_target;
        scsi_release_disk(sdkp);
        scsi_release_device(sdev);
        scsi_target_reap(shost, starget);
        return -1;
    }
    if (scsi_store_host_entry(shost, sdev, sdkp) != 0) {
        struct scsi_target *starget = sdev->sdev_target;
        scsi_release_disk(sdkp);
        scsi_release_device(sdev);
        scsi_target_reap(shost, starget);
        return -1;
    }
    return 0;
}

int scsi_scan_target(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun)
{
    if (!shost)
        return -1;
    if (channel > shost->max_channel || id >= shost->max_id)
        return -1;
    if (lun != SCSI_SCAN_WILD_CARD_LUN && lun >= shost->max_lun)
        return -1;

    if (lun == SCSI_SCAN_WILD_CARD_LUN) {
        /*
         * Linux scans LUN 0 first and only expands to REPORT LUNS when the
         * target answers. Lite keeps that ordering, but still falls back to
         * the already discovered LUN 0 when REPORT LUNS is unsupported.
         */
        if (scsi_probe_and_add_lun(shost, channel, id, 0) != 0)
            return -1;
        if (shost->max_lun <= 1)
            return 0;
        struct scsi_target *starget = scsi_lookup_target(shost, channel, id);
        if (starget && (starget->flags & SCSI_TARGET_NO_REPORT_LUNS))
            return scsi_sequential_lun_scan(shost, channel, id);
        if (scsi_report_lun_scan(shost, channel, id) == 0)
            return 0;
        return scsi_sequential_lun_scan(shost, channel, id);
    }
    return scsi_probe_and_add_lun(shost, channel, id, lun);
}

int scsi_scan_host_selected(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun)
{
    if (!shost)
        return -1;
    if (((channel != SCSI_SCAN_WILD_CARD) && (channel > shost->max_channel)) ||
        ((id != SCSI_SCAN_WILD_CARD) && (id >= shost->max_id)) ||
        ((lun != SCSI_SCAN_WILD_CARD_LUN) && (lun >= shost->max_lun)))
        return -1;

    uint32_t ch_lo = channel == SCSI_SCAN_WILD_CARD ? 0 : channel;
    uint32_t ch_hi = channel == SCSI_SCAN_WILD_CARD ? (uint32_t)shost->max_channel + 1 : channel + 1;
    uint32_t id_lo = id == SCSI_SCAN_WILD_CARD ? 0 : id;
    uint32_t id_hi = id == SCSI_SCAN_WILD_CARD ? shost->max_id : id + 1;
    for (uint32_t scan_channel = ch_lo; scan_channel < ch_hi; scan_channel++) {
        for (uint32_t scan_id = id_lo; scan_id < id_hi; scan_id++)
            (void)scsi_scan_target(shost, scan_channel, scan_id, lun);
    }
    return 0;
}

int scsi_scan_host(struct Scsi_Host *shost)
{
    return scsi_scan_host_selected(shost, SCSI_SCAN_WILD_CARD, SCSI_SCAN_WILD_CARD, SCSI_SCAN_WILD_CARD_LUN);
}

void scsi_remove_host(struct Scsi_Host *shost)
{
    if (!shost)
        return;
    while (shost->nr_devices > 0) {
        shost->nr_devices--;
        scsi_release_disk(shost->sdks[shost->nr_devices]);
        scsi_release_device(shost->sdevs[shost->nr_devices]);
    }
    while (shost->nr_targets > 0) {
        shost->nr_targets--;
        scsi_release_target(shost->stargets[shost->nr_targets]);
        shost->stargets[shost->nr_targets] = NULL;
    }
}
