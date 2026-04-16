#ifndef LITE_NVME_INTERNAL_H
#define LITE_NVME_INTERNAL_H

#include "linux/device.h"
#include "linux/pci.h"
#include "linux/blkdev.h"
#include "linux/blk_queue.h"
#include <stdint.h>

/*
 * Internal NVMe driver definitions (not part of a stable public ABI).
 * Linux mapping: nvme host driver keeps private structs in drivers/nvme/host/nvme.h.
 */

struct nvme_queue {
    uint16_t qid;
    uint16_t depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t cq_phase;
    uint16_t next_cid;
    void *sq;
    void *cq;
    uint32_t sq_phys;
    uint32_t cq_phys;
    volatile uint32_t *sq_db;
    volatile uint32_t *cq_db;
};

struct nvme_dev {
    struct pci_dev *pdev;
    uint32_t instance;
    void *mmio;
    uint64_t cap;
    uint32_t vs;
    uint32_t db_stride;
    uint16_t mqes; /* CAP.MQES (0-based) */
    uint8_t cqr;   /* CAP.CQR: queues required to be physically contiguous */
    struct nvme_queue admin_q;
    struct nvme_queue io_q;

    /* Linux mapping: /sys/class/nvme/nvmeX controller device. */
    struct device ctrl_dev;
    int ctrl_registered;
};

struct nvme_ns {
    struct nvme_dev *dev;
    uint32_t instance;
    uint32_t nsid;
    uint64_t size_bytes;
    uint32_t lba_size;
    struct request_queue *queue;
    struct gendisk *disk;
};

int nvme_ctrl_register(struct nvme_dev *dev);
void nvme_ctrl_unregister(struct nvme_dev *dev);

#endif /* LITE_NVME_INTERNAL_H */

