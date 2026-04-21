#include "linux/libc.h"
#include "linux/memlayout.h"
#include "linux/page_alloc.h"
#include "linux/pci_regs.h"
#include "linux/slab.h"
#include "linux/vmalloc.h"
#include "virtio_pci_common.h"

struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __attribute__((packed));

static void vp_notify_modern(struct virtqueue *vq)
{
    if (!vq || !vq->notify_addr)
        return;
    __asm__ volatile ("" ::: "memory");
    *(volatile uint16_t *)vq->notify_addr = vq->index;
}

static uint64_t vp_get_features_modern(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    volatile struct virtio_pci_common_cfg *cfg;
    uint32_t lo;
    uint32_t hi;

    if (!vpdev || !vpdev->common_cfg)
        return 0;
    cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    cfg->device_feature_select = 0;
    __asm__ volatile ("" ::: "memory");
    lo = cfg->device_feature;
    cfg->device_feature_select = 1;
    __asm__ volatile ("" ::: "memory");
    hi = cfg->device_feature;
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static void vp_set_features_modern(struct virtio_device *vdev, uint64_t features)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    volatile struct virtio_pci_common_cfg *cfg;

    if (!vpdev || !vpdev->common_cfg)
        return;
    cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    cfg->driver_feature_select = 0;
    cfg->driver_feature = (uint32_t)(features & 0xFFFFFFFFu);
    cfg->driver_feature_select = 1;
    cfg->driver_feature = (uint32_t)(features >> 32);
    __asm__ volatile ("" ::: "memory");
}

static uint8_t vp_get_status_modern(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    volatile struct virtio_pci_common_cfg *cfg;

    if (!vpdev || !vpdev->common_cfg)
        return 0;
    cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    return cfg->device_status;
}

static void vp_set_status_modern(struct virtio_device *vdev, uint8_t status)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    volatile struct virtio_pci_common_cfg *cfg;

    if (!vpdev || !vpdev->common_cfg)
        return;
    cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    cfg->device_status = status;
    __asm__ volatile ("" ::: "memory");
}

static void vp_reset_modern(struct virtio_device *vdev)
{
    vp_set_status_modern(vdev, 0);
}

static int vp_get_config_modern(struct virtio_device *vdev, uint32_t offset, void *buf, uint32_t len)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    volatile const uint8_t *src;
    uint8_t *dst;

    if (!vpdev || !vpdev->device_cfg || !buf || !len)
        return 0;
    if (offset + len > vpdev->device_cfg_len)
        return -1;
    src = (volatile const uint8_t *)vpdev->device_cfg;
    dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = src[offset + i];
    return 0;
}

static int vp_find_vqs_modern(struct virtio_device *vdev, uint16_t nvqs, struct virtqueue **vqs)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    volatile struct virtio_pci_common_cfg *cfg;

    if (!vdev || !vqs || nvqs == 0 || nvqs > 8)
        return -1;
    if (!vpdev || !vpdev->common_cfg || !vpdev->notify_base || vpdev->notify_off_multiplier == 0)
        return -1;
    cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;

    vp_del_vqs(vdev);
    for (uint16_t i = 0; i < nvqs; i++) {
        uint16_t qnum;
        uint32_t ring_size;
        unsigned int order;
        void *phys;
        uint32_t ring_phys;
        void *ring_virt;
        struct virtqueue *vq;
        uint16_t notify_off;

        cfg->queue_select = i;
        __asm__ volatile ("" ::: "memory");
        qnum = cfg->queue_size;
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
                                 vp_notify_modern, NULL);
        if (!vq) {
            free_pages(ring_phys, order);
            vp_del_vqs(vdev);
            return -1;
        }

        cfg->queue_desc = vp_vq_region_phys(vq, vq->vr.desc);
        cfg->queue_avail = vp_vq_region_phys(vq, vq->vr.avail);
        cfg->queue_used = vp_vq_region_phys(vq, vq->vr.used);
        __asm__ volatile ("" ::: "memory");
        cfg->queue_enable = 1;

        notify_off = cfg->queue_notify_off;
        vq->notify_addr = (void *)(vpdev->notify_base +
                                   (uint32_t)notify_off * vpdev->notify_off_multiplier);

        vqs[i] = vq;
        vpdev->vqs[i] = vq;
        vpdev->nvqs = (uint16_t)(i + 1);
    }
    return 0;
}

const struct virtio_config_ops vp_modern_config_ops = {
    .get_features = vp_get_features_modern,
    .set_features = vp_set_features_modern,
    .get_status = vp_get_status_modern,
    .set_status = vp_set_status_modern,
    .reset = vp_reset_modern,
    .get_config = vp_get_config_modern,
    .find_vqs = vp_find_vqs_modern,
    .del_vqs = vp_del_vqs,
};

static void *vp_cap_map_region(struct virtio_pci_device *vpdev, uint8_t bar, uint32_t offset)
{
    uint32_t barv;
    uint32_t base;

    if (!vpdev || !vpdev->pdev || bar >= 6)
        return NULL;
    barv = vpdev->pdev->bar[bar];
    if (barv & 0x1)
        return NULL;
    base = barv & ~0xFu;
    return ioremap(base + offset, 4096);
}

void vp_parse_modern_caps(struct virtio_pci_device *vpdev)
{
    struct pci_dev *pdev;
    uint8_t cap = 0;

    if (!vpdev || !vpdev->pdev)
        return;
    pdev = vpdev->pdev;
    if (pci_read_config_byte(pdev, 0x34, &cap) != 0 || !cap)
        return;
    while (cap) {
        uint8_t id = 0;
        uint8_t next = 0;

        if (pci_read_config_byte(pdev, cap, &id) != 0)
            break;
        if (pci_read_config_byte(pdev, cap + 1, &next) != 0)
            break;

        if (id == PCI_CAP_ID_VNDR) {
            uint8_t cfg_type = 0;
            uint8_t bar = 0;
            uint32_t off = 0;
            uint32_t len = 0;

            pci_read_config_byte(pdev, cap + 3, &cfg_type);
            pci_read_config_byte(pdev, cap + 4, &bar);
            pci_read_config_dword(pdev, cap + 8, &off);
            pci_read_config_dword(pdev, cap + 12, &len);

            if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                vpdev->common_cfg = vp_cap_map_region(vpdev, bar, off);
            } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                vpdev->isr_cfg = (volatile uint8_t *)vp_cap_map_region(vpdev, bar, off);
            } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                vpdev->device_cfg = (volatile uint8_t *)vp_cap_map_region(vpdev, bar, off);
                vpdev->device_cfg_len = len;
            } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                uint32_t mult = 0;

                pci_read_config_dword(pdev, cap + 16, &mult);
                vpdev->notify_base = (volatile uint8_t *)vp_cap_map_region(vpdev, bar, off);
                vpdev->notify_off_multiplier = mult;
            }
        }

        cap = next;
    }

    if (vpdev->common_cfg && vpdev->notify_base &&
        vpdev->device_cfg && vpdev->notify_off_multiplier)
        vpdev->modern = 1;
}
