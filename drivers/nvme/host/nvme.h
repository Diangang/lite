#ifndef LITE_DRIVERS_NVME_HOST_NVME_H
#define LITE_DRIVERS_NVME_HOST_NVME_H

#include "linux/device.h"
#include "linux/pci.h"
#include "linux/blkdev.h"
#include <stdint.h>

/*
 * Linux mapping: private NVMe host controller structs live in
 * drivers/nvme/host/nvme.h.
 */

struct nvme_queue;
struct nvme_ns;

struct nvme_dev {
    struct pci_dev *pdev;
    uint32_t instance;
    char name[12];
    void *bar;
    uint64_t cap;
    uint32_t db_stride;
    uint32_t ctrl_config;
    uint32_t page_size;
    uint16_t mqes;
    struct nvme_queue *admin_q;
    struct nvme_queue *io_q;
    /* Lite subset: only one namespace is supported currently. */
    struct nvme_ns *ns;

    /* Linux mapping: /sys/class/nvme/nvmeX controller device. */
    struct device *device;
};

struct nvme_ns {
    struct nvme_dev *dev;
    uint32_t ns_id;
    uint64_t size_bytes;
    uint32_t lba_shift;
    struct request_queue *queue;
    struct gendisk *disk;
};

#endif
