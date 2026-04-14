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

/* Virtio status bits */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     0x01
#define VIRTIO_CONFIG_S_DRIVER          0x02
#define VIRTIO_CONFIG_S_DRIVER_OK       0x04
#define VIRTIO_CONFIG_S_FEATURES_OK     0x08
#define VIRTIO_CONFIG_S_FAILED          0x80

#endif
