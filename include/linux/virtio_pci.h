#ifndef LINUX_VIRTIO_PCI_H
#define LINUX_VIRTIO_PCI_H

#include <stdint.h>

/* Linux uapi/linux/virtio_pci.h legacy/transitional subset */
#define VIRTIO_PCI_HOST_FEATURES        0
#define VIRTIO_PCI_GUEST_FEATURES       4
#define VIRTIO_PCI_QUEUE_PFN            8
#define VIRTIO_PCI_QUEUE_NUM            12
#define VIRTIO_PCI_QUEUE_SEL            14
#define VIRTIO_PCI_QUEUE_NOTIFY         16
#define VIRTIO_PCI_STATUS               18
#define VIRTIO_PCI_ISR                  19
#define VIRTIO_PCI_CONFIG_OFF(msix)     ((msix) ? 24 : 20)
#define VIRTIO_PCI_ABI_VERSION          0
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT     12
#define VIRTIO_PCI_VRING_ALIGN          4096

/*
 * Modern (PCI 1.0) virtio-pci vendor capabilities.
 *
 * Linux mapping:
 * - include/uapi/linux/virtio_pci.h: struct virtio_pci_cap, VIRTIO_PCI_CAP_*
 * - include/uapi/linux/virtio_config.h: status bits (already defined elsewhere too)
 */
#define VIRTIO_PCI_CAP_COMMON_CFG  1
#define VIRTIO_PCI_CAP_NOTIFY_CFG  2
#define VIRTIO_PCI_CAP_ISR_CFG     3
#define VIRTIO_PCI_CAP_DEVICE_CFG  4
#define VIRTIO_PCI_CAP_PCI_CFG     5

struct virtio_pci_cap {
    uint8_t cap_vndr;   /* PCI_CAP_ID_VNDR */
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;   /* VIRTIO_PCI_CAP_* */
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

struct virtio_pci_notify_cap {
    struct virtio_pci_cap cap;
    uint32_t notify_off_multiplier;
} __attribute__((packed));

/* Virtio status bits */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     0x01
#define VIRTIO_CONFIG_S_DRIVER          0x02
#define VIRTIO_CONFIG_S_DRIVER_OK       0x04
#define VIRTIO_CONFIG_S_FEATURES_OK     0x08
#define VIRTIO_CONFIG_S_FAILED          0x80

#endif
