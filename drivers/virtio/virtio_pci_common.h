#ifndef _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H
#define _DRIVERS_VIRTIO_VIRTIO_PCI_COMMON_H

#include "linux/pci.h"
#include "linux/virtio.h"
#include "linux/virtio_pci.h"
#include "linux/virtio_ring.h"

#define PCI_VENDOR_ID_QUMRANET 0x1AF4
#define PCI_DEVICE_ID_VIRTIO_SCSI_LEGACY 0x1004
#define PCI_DEVICE_ID_VIRTIO_SCSI_MODERN 0x1048

struct virtio_pci_device {
    struct virtio_device vdev;
    struct pci_dev *pdev;
    uint16_t nvqs;
    struct virtqueue *vqs[8];

    /* Legacy I/O port base (transitional devices expose this at BAR0). */
    uint16_t ioaddr;

    /* Modern (PCI 1.0) capability regions (MMIO). */
    int modern;
    volatile void *common_cfg;
    volatile uint8_t *isr_cfg;
    volatile uint8_t *device_cfg;
    volatile uint8_t *notify_base;
    uint32_t notify_off_multiplier;
    uint32_t device_cfg_len;
};

static inline struct virtio_pci_device *to_vpdev(struct virtio_device *vdev)
{
    return container_of(vdev, struct virtio_pci_device, vdev);
}

static inline unsigned int vp_order_for_size(uint32_t size)
{
    unsigned int order = 0;
    uint32_t bytes = 4096;

    while (bytes < size) {
        bytes <<= 1;
        order++;
    }
    return order;
}

static inline uint64_t vp_vq_region_phys(struct virtqueue *vq, void *ptr)
{
    uintptr_t base = (uintptr_t)vq->ring_virt;
    uintptr_t p = (uintptr_t)ptr;

    return (uint64_t)vq->ring_phys + (uint64_t)(p - base);
}

void vp_del_vqs(struct virtio_device *vdev);
void vp_parse_modern_caps(struct virtio_pci_device *vpdev);

extern const struct virtio_config_ops vp_legacy_config_ops;
extern const struct virtio_config_ops vp_modern_config_ops;

#endif
