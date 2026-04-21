#include "linux/libc.h"
#include "linux/memlayout.h"
#include "linux/page_alloc.h"
#include "linux/slab.h"
#include "virtio_pci_common.h"

static uint64_t vp_get_features(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);

    return (uint64_t)inl((uint16_t)(vpdev->ioaddr + VIRTIO_PCI_HOST_FEATURES));
}

static void vp_set_features(struct virtio_device *vdev, uint64_t features)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);

    outl((uint16_t)(vpdev->ioaddr + VIRTIO_PCI_GUEST_FEATURES), (uint32_t)features);
}

static uint8_t vp_get_status(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);

    return inb((uint16_t)(vpdev->ioaddr + VIRTIO_PCI_STATUS));
}

static void vp_set_status(struct virtio_device *vdev, uint8_t status)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);

    outb((uint16_t)(vpdev->ioaddr + VIRTIO_PCI_STATUS), status);
}

static void vp_reset(struct virtio_device *vdev)
{
    vp_set_status(vdev, 0);
}

static int vp_get_config(struct virtio_device *vdev, uint32_t offset, void *buf, uint32_t len)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    uint16_t base;
    uint8_t *p;

    if (!buf || !len)
        return 0;
    if (!vpdev->ioaddr)
        return -1;
    base = (uint16_t)(vpdev->ioaddr + VIRTIO_PCI_CONFIG_OFF(0));
    p = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        p[i] = inb((uint16_t)(base + offset + i));
    return 0;
}

static void vp_notify_legacy(struct virtqueue *vq)
{
    struct virtio_pci_device *vpdev;

    if (!vq || !vq->vdev)
        return;
    vpdev = to_vpdev(vq->vdev);
    if (!vpdev || !vpdev->ioaddr)
        return;
    outw((uint16_t)(vpdev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY), vq->index);
}

static int vp_find_vqs_legacy(struct virtio_device *vdev, uint16_t nvqs, struct virtqueue **vqs)
{
    struct virtio_pci_device *vpdev;
    uint16_t ioaddr;

    if (!vdev || !vqs || nvqs == 0 || nvqs > 8)
        return -1;
    vpdev = to_vpdev(vdev);
    if (!vpdev || !vpdev->ioaddr)
        return -1;
    ioaddr = vpdev->ioaddr;

    vp_del_vqs(vdev);
    for (uint16_t i = 0; i < nvqs; i++) {
        uint16_t qnum;
        uint32_t ring_size;
        unsigned int order;
        void *phys;
        uint32_t ring_phys;
        void *ring_virt;
        struct virtqueue *vq;

        outw((uint16_t)(ioaddr + VIRTIO_PCI_QUEUE_SEL), i);
        qnum = inw((uint16_t)(ioaddr + VIRTIO_PCI_QUEUE_NUM));
        if (qnum == 0) {
            vp_del_vqs(vdev);
            return -1;
        }

        ring_size = vring_size(qnum, VIRTIO_PCI_VRING_ALIGN);
        order = vp_order_for_size(ring_size);
        phys = alloc_pages(GFP_KERNEL, order);
        if (!phys) {
            vp_del_vqs(vdev);
            return -1;
        }
        ring_phys = (uint32_t)phys;
        ring_virt = memlayout_directmap_phys_to_virt(ring_phys);
        memset(ring_virt, 0, 4096u << order);

        vq = vring_new_virtqueue(i, qnum, VIRTIO_PCI_VRING_ALIGN, vdev,
                                 ring_virt, ring_phys, order,
                                 vp_notify_legacy, NULL);
        if (!vq) {
            free_pages(ring_phys, order);
            vp_del_vqs(vdev);
            return -1;
        }

        outl((uint16_t)(ioaddr + VIRTIO_PCI_QUEUE_PFN),
             (uint32_t)(ring_phys >> VIRTIO_PCI_QUEUE_ADDR_SHIFT));
        vqs[i] = vq;
        vpdev->vqs[i] = vq;
        vpdev->nvqs = (uint16_t)(i + 1);
    }
    return 0;
}

const struct virtio_config_ops vp_legacy_config_ops = {
    .get_features = vp_get_features,
    .set_features = vp_set_features,
    .get_status = vp_get_status,
    .set_status = vp_set_status,
    .reset = vp_reset,
    .get_config = vp_get_config,
    .find_vqs = vp_find_vqs_legacy,
    .del_vqs = vp_del_vqs,
};
