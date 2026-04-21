#ifndef LINUX_VIRTIO_SCSI_H
#define LINUX_VIRTIO_SCSI_H

#include <stdint.h>

#define VIRTIO_SCSI_CDB_DEFAULT_SIZE   32
#define VIRTIO_SCSI_SENSE_DEFAULT_SIZE 96

struct virtio_scsi_cmd_req {
    uint8_t lun[8];
    uint64_t tag;
    uint8_t task_attr;
    uint8_t prio;
    uint8_t crn;
    uint8_t cdb[VIRTIO_SCSI_CDB_DEFAULT_SIZE];
} __attribute__((packed));

struct virtio_scsi_cmd_resp {
    uint32_t sense_len;
    uint32_t resid;
    uint16_t status_qualifier;
    uint8_t status;
    uint8_t response;
    uint8_t sense[VIRTIO_SCSI_SENSE_DEFAULT_SIZE];
} __attribute__((packed));

struct virtio_scsi_event {
    uint32_t event;
    uint8_t lun[8];
    uint32_t reason;
} __attribute__((packed));

struct virtio_scsi_config {
    uint32_t num_queues;
    uint32_t seg_max;
    uint32_t max_sectors;
    uint32_t cmd_per_lun;
    uint32_t event_info_size;
    uint32_t sense_size;
    uint32_t cdb_size;
    uint16_t max_channel;
    uint16_t max_target;
    uint32_t max_lun;
} __attribute__((packed));

#define VIRTIO_SCSI_S_OK                0
#define VIRTIO_SCSI_S_SIMPLE            0

#endif
