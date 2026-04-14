#include "scsi/scsi.h"
#include "scsi/scsi_host.h"
#include "linux/init.h"
#include "linux/blk_queue.h"
#include "linux/libc.h"
#include "linux/slab.h"

static struct class scsi_host_class;
static struct class scsi_device_class;
static struct class scsi_disk_class;
static uint32_t scsi_host_next;
static uint32_t scsi_disk_next;
static const uint32_t scsi_scan_tur_retries = 3;

struct scsi_lun {
    uint8_t scsi_lun[8];
};

static int scsi_probe_and_add_lun(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun);

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
    device_set_parent(&sdev->sdev_gendev, &sdev->host->shost_gendev);
    return device_add(&sdev->sdev_gendev);
}

static void scsi_release_disk(struct scsi_disk *sdkp)
{
    if (!sdkp)
        return;
    if (sdkp->disk) {
        if (sdkp->disk->dev)
            device_unregister(sdkp->disk->dev);
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
    return sdev->host->hostt->queuecommand(sdev->host, sdev, cdb, cdb_len, data, data_len,
                                           dir, sense, sense_len, status);
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

static int scsi_report_lun_scan(struct Scsi_Host *shost, uint32_t channel, uint32_t id)
{
    if (!shost)
        return -1;
    if (!shost->max_lun)
        return -1;

    struct scsi_device *sdev = scsi_alloc_device(shost, channel, id, 0);
    if (!sdev)
        return -1;

    uint64_t alloc_len64 = 8ULL + ((uint64_t)shost->max_lun * sizeof(struct scsi_lun));
    if (alloc_len64 > 0xFFFFFFFFULL) {
        kfree(sdev);
        return -1;
    }
    uint32_t alloc_len = (uint32_t)alloc_len64;
    uint8_t *buf = (uint8_t *)kmalloc(alloc_len);
    if (!buf) {
        kfree(sdev);
        return -1;
    }
    memset(buf, 0, alloc_len);

    uint8_t cdb[12];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = REPORT_LUNS;
    cdb[6] = (uint8_t)(alloc_len >> 24);
    cdb[7] = (uint8_t)(alloc_len >> 16);
    cdb[8] = (uint8_t)(alloc_len >> 8);
    cdb[9] = (uint8_t)alloc_len;

    int ret = -1;
    for (uint32_t attempt = 0; attempt < scsi_scan_tur_retries && ret != 0; attempt++)
        ret = scsi_execute_cmd(sdev, cdb, sizeof(cdb), buf, alloc_len, SCSI_DATA_READ, NULL, NULL, NULL);
    if (ret != 0) {
        kfree(buf);
        kfree(sdev);
        return -1;
    }

    uint32_t length = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                      ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    if (length + 8U > alloc_len)
        length = alloc_len - 8U;

    uint32_t num_luns = length / sizeof(struct scsi_lun);
    struct scsi_lun *luns = (struct scsi_lun *)(buf + 8);
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
        kfree(sdev);
        return -1;
    }

    struct scsi_disk *sdkp = scsi_alloc_disk(sdev);
    if (!sdkp) {
        scsi_release_device(sdev);
        return -1;
    }
    ret = scsi_add_disk(sdkp);
    if (ret != 0) {
        scsi_release_disk(sdkp);
        scsi_release_device(sdev);
        return -1;
    }
    if (scsi_store_host_entry(shost, sdev, sdkp) != 0) {
        scsi_release_disk(sdkp);
        scsi_release_device(sdev);
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
        if (scsi_report_lun_scan(shost, channel, id) == 0)
            return 0;
        return 0;
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
}

static int scsi_classes_init(void)
{
    scsi_host_class.name = "scsi_host";
    INIT_LIST_HEAD(&scsi_host_class.list);
    INIT_LIST_HEAD(&scsi_host_class.devices);
    if (class_register(&scsi_host_class) != 0)
        return -1;

    scsi_device_class.name = "scsi_device";
    INIT_LIST_HEAD(&scsi_device_class.list);
    INIT_LIST_HEAD(&scsi_device_class.devices);
    if (class_register(&scsi_device_class) != 0)
        return -1;

    scsi_disk_class.name = "scsi_disk";
    INIT_LIST_HEAD(&scsi_disk_class.list);
    INIT_LIST_HEAD(&scsi_disk_class.devices);
    return class_register(&scsi_disk_class);
}
core_initcall(scsi_classes_init);
