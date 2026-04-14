#ifndef SCSI_SCSI_HOST_H
#define SCSI_SCSI_HOST_H

#include <stdint.h>
#include "linux/device.h"
#include "linux/blkdev.h"

#define SCSI_SCAN_WILD_CARD 0xFFFFFFFFU
#define SCSI_SCAN_WILD_CARD_LUN (~0ULL)
#define SCSI_HOST_MAX_DEVICES 8

struct Scsi_Host;
struct scsi_device;
struct scsi_disk;

struct scsi_host_template {
    const char *name;
    int (*queuecommand)(struct Scsi_Host *shost, struct scsi_device *sdev,
                        const uint8_t *cdb, uint32_t cdb_len,
                        void *data, uint32_t data_len, int dir,
                        uint8_t *sense, uint32_t *sense_len, uint8_t *status);
};

struct Scsi_Host {
    struct device shost_gendev;
    struct scsi_host_template *hostt;
    void *hostdata;
    uint32_t host_no;
    uint16_t max_channel;
    uint16_t max_id;
    uint32_t max_lun;
    uint32_t nr_devices;
    struct scsi_device *sdevs[SCSI_HOST_MAX_DEVICES];
    struct scsi_disk *sdks[SCSI_HOST_MAX_DEVICES];
    char name[16];
};

struct scsi_device {
    struct Scsi_Host *host;
    struct device sdev_gendev;
    uint32_t channel;
    uint32_t id;
    uint64_t lun;
    uint8_t type;
    uint32_t sector_size;
    uint64_t capacity_sectors;
    char vendor[9];
    char model[17];
    char name[32];
    void *hostdata;
};

struct scsi_disk {
    struct scsi_device *device;
    struct device dev;
    struct gendisk *disk;
    uint32_t index;
    char name[32];
};

struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *sht, void *hostdata);
int scsi_add_host(struct Scsi_Host *shost, struct device *parent);
struct scsi_device *scsi_alloc_device(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun);
int scsi_add_device(struct scsi_device *sdev);
struct scsi_disk *scsi_alloc_disk(struct scsi_device *sdev);
int scsi_add_disk(struct scsi_disk *sdkp);
int scsi_scan_target(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun);
int scsi_scan_host_selected(struct Scsi_Host *shost, uint32_t channel, uint32_t id, uint64_t lun);
int scsi_scan_host(struct Scsi_Host *shost);
void scsi_remove_host(struct Scsi_Host *shost);

int scsi_execute_cmd(struct scsi_device *sdev, const uint8_t *cdb, uint32_t cdb_len,
                     void *data, uint32_t data_len, int dir,
                     uint8_t *sense, uint32_t *sense_len, uint8_t *status);
int scsi_test_unit_ready(struct scsi_device *sdev);
int scsi_inquiry(struct scsi_device *sdev, uint8_t *buf, uint32_t len);
int scsi_read_capacity(struct scsi_device *sdev, uint32_t *last_lba, uint32_t *block_size);
int scsi_read10(struct scsi_device *sdev, uint32_t lba, uint16_t blocks, void *buf, uint32_t len);
int scsi_write10(struct scsi_device *sdev, uint32_t lba, uint16_t blocks, const void *buf, uint32_t len);

#endif
