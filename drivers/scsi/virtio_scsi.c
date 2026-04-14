#include "linux/libc.h"
#include "linux/types.h"
#include "linux/init.h"
#include "linux/page_alloc.h"
#include "linux/pci.h"
#include "linux/slab.h"
#include "linux/timer.h"
#include "linux/time.h"
#include "linux/virtio_ids.h"
#include "linux/virtio_pci.h"
#include "linux/virtio_ring.h"
#include "linux/virtio_scsi.h"
#include "linux/blk_queue.h"
#include "linux/memlayout.h"
#include "asm/pgtable.h"
#include "scsi/scsi.h"
#include "scsi/scsi_host.h"

#define PCI_VENDOR_ID_QUMRANET 0x1AF4
#define PCI_DEVICE_ID_VIRTIO_SCSI_LEGACY 0x1004
#define PCI_DEVICE_ID_VIRTIO_SCSI_MODERN 0x1048

#define VIRTIO_SCSI_QUEUE_CTRL  0
#define VIRTIO_SCSI_QUEUE_EVENT 1
#define VIRTIO_SCSI_QUEUE_REQ   2

struct virtio_scsi_vq {
    uint16_t index;
    uint16_t qnum;
    uint16_t last_used_idx;
    uint32_t ring_phys;
    void *ring_virt;
    unsigned int ring_order;
    struct vring vr;
    uint32_t evt_phys;
    struct virtio_scsi_event *evt;
};

struct virtio_scsi {
    struct pci_dev *pdev;
    uint16_t ioaddr;
    struct virtio_scsi_config cfg;
    struct virtio_scsi_vq ctrl_vq;
    struct virtio_scsi_vq event_vq;
    struct virtio_scsi_vq req_vq;
    struct Scsi_Host *host;
};

static void virtscsi_remove(struct pci_dev *pdev);

static uint16_t virtio_pci_read16(struct virtio_scsi *vscsi, uint16_t off)
{
    return inw((uint16_t)(vscsi->ioaddr + off));
}

static uint32_t virtio_pci_read32(struct virtio_scsi *vscsi, uint16_t off)
{
    return inl((uint16_t)(vscsi->ioaddr + off));
}

static void virtio_pci_write8(struct virtio_scsi *vscsi, uint16_t off, uint8_t v)
{
    outb((uint16_t)(vscsi->ioaddr + off), v);
}

static void virtio_pci_write16(struct virtio_scsi *vscsi, uint16_t off, uint16_t v)
{
    outw((uint16_t)(vscsi->ioaddr + off), v);
}

static void virtio_pci_write32(struct virtio_scsi *vscsi, uint16_t off, uint32_t v)
{
    outl((uint16_t)(vscsi->ioaddr + off), v);
}

static unsigned int virtio_order_for_size(uint32_t size)
{
    unsigned int order = 0;
    uint32_t bytes = 4096;
    while (bytes < size) {
        bytes <<= 1;
        order++;
    }
    return order;
}

static int virtio_scsi_read_config(struct virtio_scsi *vscsi)
{
    uint16_t off = VIRTIO_PCI_CONFIG_OFF(0);
    vscsi->cfg.num_queues = virtio_pci_read32(vscsi, off + 0);
    vscsi->cfg.seg_max = virtio_pci_read32(vscsi, off + 4);
    vscsi->cfg.max_sectors = virtio_pci_read32(vscsi, off + 8);
    vscsi->cfg.cmd_per_lun = virtio_pci_read32(vscsi, off + 12);
    vscsi->cfg.event_info_size = virtio_pci_read32(vscsi, off + 16);
    vscsi->cfg.sense_size = virtio_pci_read32(vscsi, off + 20);
    vscsi->cfg.cdb_size = virtio_pci_read32(vscsi, off + 24);
    vscsi->cfg.max_channel = virtio_pci_read16(vscsi, off + 28);
    vscsi->cfg.max_target = virtio_pci_read16(vscsi, off + 30);
    vscsi->cfg.max_lun = virtio_pci_read32(vscsi, off + 32);
    return 0;
}

static int virtio_scsi_setup_vq(struct virtio_scsi *vscsi, struct virtio_scsi_vq *vq, uint16_t index)
{
    if (!vscsi || !vq)
        return -1;
    memset(vq, 0, sizeof(*vq));
    vq->index = index;
    virtio_pci_write16(vscsi, VIRTIO_PCI_QUEUE_SEL, index);
    vq->qnum = virtio_pci_read16(vscsi, VIRTIO_PCI_QUEUE_NUM);
    if (vq->qnum == 0)
        return -1;
    uint32_t ring_size = vring_size(vq->qnum, VIRTIO_PCI_VRING_ALIGN);
    vq->ring_order = virtio_order_for_size(ring_size);
    void *phys = alloc_pages(GFP_KERNEL, vq->ring_order);
    if (!phys)
        return -1;
    vq->ring_phys = (uint32_t)phys;
    vq->ring_virt = memlayout_directmap_phys_to_virt(vq->ring_phys);
    memset(vq->ring_virt, 0, 4096u << vq->ring_order);
    vring_init(&vq->vr, vq->qnum, vq->ring_virt, VIRTIO_PCI_VRING_ALIGN);
    virtio_pci_write32(vscsi, VIRTIO_PCI_QUEUE_PFN, vq->ring_phys >> VIRTIO_PCI_QUEUE_ADDR_SHIFT);
    return 0;
}

static int virtio_scsi_prime_eventq(struct virtio_scsi *vscsi)
{
    struct virtio_scsi_vq *vq = &vscsi->event_vq;
    void *evt_phys = alloc_page(GFP_KERNEL);
    if (!evt_phys)
        return -1;
    vq->evt_phys = (uint32_t)evt_phys;
    vq->evt = (struct virtio_scsi_event *)memlayout_directmap_phys_to_virt(vq->evt_phys);
    memset(vq->evt, 0, sizeof(*vq->evt));
    vq->vr.desc[0].addr = vq->evt_phys;
    vq->vr.desc[0].len = sizeof(*vq->evt);
    vq->vr.desc[0].flags = VRING_DESC_F_WRITE;
    vq->vr.desc[0].next = 0;
    vq->vr.avail->ring[0] = 0;
    __asm__ volatile ("" ::: "memory");
    vq->vr.avail->idx = 1;
    virtio_pci_write16(vscsi, VIRTIO_PCI_QUEUE_NOTIFY, vq->index);
    return 0;
}

static void virtio_scsi_init_lun(struct virtio_scsi_cmd_req *req, struct scsi_device *sdev)
{
    memset(req, 0, sizeof(*req));
    req->lun[0] = 1;
    req->lun[1] = (uint8_t)sdev->id;
    req->lun[2] = (uint8_t)(((uint16_t)sdev->lun >> 8) | 0x40);
    req->lun[3] = (uint8_t)sdev->lun;
    req->task_attr = VIRTIO_SCSI_S_SIMPLE;
}

static int virtio_scsi_submit(struct virtio_scsi *vscsi, struct scsi_device *sdev,
                              const uint8_t *cdb, uint32_t cdb_len,
                              void *data, uint32_t data_len, int dir,
                              uint8_t *sense, uint32_t *sense_len, uint8_t *status)
{
    if (!vscsi || !sdev || !cdb || cdb_len > VIRTIO_SCSI_CDB_DEFAULT_SIZE)
        return -1;
    struct virtio_scsi_vq *vq = &vscsi->req_vq;
    struct virtio_scsi_cmd_req req;
    struct virtio_scsi_cmd_resp resp;
    memset(&resp, 0, sizeof(resp));
    virtio_scsi_init_lun(&req, sdev);
    memcpy(req.cdb, cdb, cdb_len);

    uint16_t desc_count = 0;
    vq->vr.desc[desc_count].addr = virt_to_phys(&req);
    vq->vr.desc[desc_count].len = sizeof(req);
    vq->vr.desc[desc_count].flags = VRING_DESC_F_NEXT;
    vq->vr.desc[desc_count].next = desc_count + 1;
    desc_count++;

    if (data && data_len && dir == SCSI_DATA_WRITE) {
        vq->vr.desc[desc_count].addr = virt_to_phys(data);
        vq->vr.desc[desc_count].len = data_len;
        vq->vr.desc[desc_count].flags = VRING_DESC_F_NEXT;
        vq->vr.desc[desc_count].next = desc_count + 1;
        desc_count++;
    }

    vq->vr.desc[desc_count].addr = virt_to_phys(&resp);
    vq->vr.desc[desc_count].len = sizeof(resp);
    vq->vr.desc[desc_count].flags = VRING_DESC_F_WRITE | ((data && data_len && dir == SCSI_DATA_READ) ? VRING_DESC_F_NEXT : 0);
    vq->vr.desc[desc_count].next = (data && data_len && dir == SCSI_DATA_READ) ? (desc_count + 1) : 0;
    desc_count++;

    if (data && data_len && dir == SCSI_DATA_READ) {
        vq->vr.desc[desc_count].addr = virt_to_phys(data);
        vq->vr.desc[desc_count].len = data_len;
        vq->vr.desc[desc_count].flags = VRING_DESC_F_WRITE;
        vq->vr.desc[desc_count].next = 0;
    }

    uint16_t slot = vq->vr.avail->idx % vq->qnum;
    vq->vr.avail->ring[slot] = 0;
    __asm__ volatile ("" ::: "memory");
    vq->vr.avail->idx++;
    virtio_pci_write16(vscsi, VIRTIO_PCI_QUEUE_NOTIFY, vq->index);

    uint32_t start = timer_get_ticks();
    while (vq->last_used_idx == vq->vr.used->idx) {
        if (timer_get_ticks() - start > HZ)
            return -1;
    }
    vq->last_used_idx++;

    if (sense && sense_len) {
        uint32_t n = resp.sense_len < *sense_len ? resp.sense_len : *sense_len;
        memcpy(sense, resp.sense, n);
        *sense_len = n;
    }
    if (status)
        *status = resp.status;
    return resp.response == VIRTIO_SCSI_S_OK && resp.status == 0 ? 0 : -1;
}

static int virtscsi_queuecommand(struct Scsi_Host *shost, struct scsi_device *sdev,
                                 const uint8_t *cdb, uint32_t cdb_len,
                                 void *data, uint32_t data_len, int dir,
                                 uint8_t *sense, uint32_t *sense_len, uint8_t *status)
{
    struct virtio_scsi *vscsi = (struct virtio_scsi *)shost->hostdata;
    return virtio_scsi_submit(vscsi, sdev, cdb, cdb_len, data, data_len, dir, sense, sense_len, status);
}

static struct scsi_host_template virtscsi_sht = {
    .name = "virtio_scsi",
    .queuecommand = virtscsi_queuecommand,
};

static int virtscsi_scan(struct virtio_scsi *vscsi)
{
    if (!vscsi)
        return -1;
    vscsi->host = scsi_host_alloc(&virtscsi_sht, vscsi);
    if (!vscsi->host)
        return -1;
    vscsi->host->max_channel = vscsi->cfg.max_channel;
    vscsi->host->max_id = (uint16_t)(vscsi->cfg.max_target + 1);
    vscsi->host->max_lun = vscsi->cfg.max_lun + 1;
    int ret = scsi_add_host(vscsi->host, &vscsi->pdev->dev);
    if (ret != 0)
        return -1;
    return scsi_scan_host(vscsi->host);
}

static int virtscsi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    (void)id;
    if (!pdev)
        return -1;
    if (!(pdev->bar[0] & 0x1))
        return -1;
    struct virtio_scsi *vscsi = (struct virtio_scsi *)kmalloc(sizeof(*vscsi));
    if (!vscsi)
        return -1;
    memset(vscsi, 0, sizeof(*vscsi));
    vscsi->pdev = pdev;
    vscsi->ioaddr = (uint16_t)(pdev->bar[0] & ~0x3u);
    pdev->dev.driver_data = vscsi;

    virtio_pci_write8(vscsi, VIRTIO_PCI_STATUS, 0);
    virtio_pci_write8(vscsi, VIRTIO_PCI_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    virtio_pci_write8(vscsi, VIRTIO_PCI_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
    virtio_pci_write32(vscsi, VIRTIO_PCI_GUEST_FEATURES, 0);

    if (virtio_scsi_read_config(vscsi) != 0)
        goto err_remove;
    if (virtio_scsi_setup_vq(vscsi, &vscsi->ctrl_vq, VIRTIO_SCSI_QUEUE_CTRL) != 0)
        goto err_remove;
    if (virtio_scsi_setup_vq(vscsi, &vscsi->event_vq, VIRTIO_SCSI_QUEUE_EVENT) != 0)
        goto err_remove;
    if (virtio_scsi_setup_vq(vscsi, &vscsi->req_vq, VIRTIO_SCSI_QUEUE_REQ) != 0)
        goto err_remove;
    if (virtio_scsi_prime_eventq(vscsi) != 0)
        goto err_remove;

    virtio_pci_write8(vscsi, VIRTIO_PCI_STATUS,
                      VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_DRIVER_OK);
    if (virtscsi_scan(vscsi) == 0)
        return 0;

err_remove:
    virtscsi_remove(pdev);
    return -1;
}

static void virtscsi_remove(struct pci_dev *pdev)
{
    if (!pdev)
        return;
    struct virtio_scsi *vscsi = (struct virtio_scsi *)pdev->dev.driver_data;
    if (!vscsi)
        return;
    scsi_remove_host(vscsi->host);
    if (vscsi->host) {
        device_unregister(&vscsi->host->shost_gendev);
        kfree(vscsi->host);
    }
    if (vscsi->event_vq.evt_phys)
        free_page(vscsi->event_vq.evt_phys);
    if (vscsi->ctrl_vq.ring_phys)
        free_pages(vscsi->ctrl_vq.ring_phys, vscsi->ctrl_vq.ring_order);
    if (vscsi->event_vq.ring_phys)
        free_pages(vscsi->event_vq.ring_phys, vscsi->event_vq.ring_order);
    if (vscsi->req_vq.ring_phys)
        free_pages(vscsi->req_vq.ring_phys, vscsi->req_vq.ring_order);
    kfree(vscsi);
    pdev->dev.driver_data = NULL;
}

static const struct pci_device_id virtscsi_id_table[] = {
    { .vendor = PCI_VENDOR_ID_QUMRANET, .device = PCI_DEVICE_ID_VIRTIO_SCSI_LEGACY },
    { .vendor = PCI_VENDOR_ID_QUMRANET, .device = PCI_DEVICE_ID_VIRTIO_SCSI_MODERN },
    { 0 }
};

static struct pci_driver virtscsi_pci_driver = {
    .name = "virtio_scsi",
    .id_table = virtscsi_id_table,
    .probe = virtscsi_probe,
    .remove = virtscsi_remove,
};

static int virtio_scsi_init(void)
{
    return pci_register_driver(&virtscsi_pci_driver);
}
module_init(virtio_scsi_init);
