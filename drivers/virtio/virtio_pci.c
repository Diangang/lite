#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/page_alloc.h"
#include "linux/pci.h"
#include "linux/slab.h"
#include "linux/virtio.h"
#include "linux/virtio_ids.h"
#include "linux/virtio_pci.h"
#include "linux/virtio_ring.h"
#include "linux/memlayout.h"
#include "linux/vmalloc.h"
#include "linux/pci_regs.h"

/*
 * Minimal virtio-pci transport for virtio-scsi.
 *
 * Stage 3 alignment goals:
 * - Keep a Linux-shaped split between common / legacy / modern.
 * - Implement modern capability parsing (PCI_CAP_ID_VNDR) and use modern config
 *   ops when available; keep legacy fallback for transitional devices.
 */

#define PCI_VENDOR_ID_QUMRANET 0x1AF4
#define PCI_DEVICE_ID_VIRTIO_SCSI_LEGACY 0x1004
#define PCI_DEVICE_ID_VIRTIO_SCSI_MODERN 0x1048

struct virtio_pci_device {
    struct virtio_device vdev;
    struct pci_dev *pdev;
    char name[16];
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

static void virtio_pci_dev_release(struct device *dev)
{
    struct virtio_device *vdev = dev_to_virtio(dev);
    struct virtio_pci_device *vpdev = container_of(vdev, struct virtio_pci_device, vdev);
    kfree(vpdev);
}

static inline struct virtio_pci_device *to_vpdev(struct virtio_device *vdev)
{
    return container_of(vdev, struct virtio_pci_device, vdev);
}

/* -------------------- Common helpers -------------------- */

static unsigned int vp_order_for_size(uint32_t size)
{
    unsigned int order = 0;
    uint32_t bytes = 4096;
    while (bytes < size) {
        bytes <<= 1;
        order++;
    }
    return order;
}

static uint64_t vp_vq_region_phys(struct virtqueue *vq, void *ptr)
{
    uintptr_t base = (uintptr_t)vq->ring_virt;
    uintptr_t p = (uintptr_t)ptr;
    return (uint64_t)vq->ring_phys + (uint64_t)(p - base);
}

static void vp_del_vqs(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    if (!vpdev)
        return;
    for (uint16_t i = 0; i < vpdev->nvqs; i++) {
        struct virtqueue *vq = vpdev->vqs[i];
        if (!vq)
            continue;
        if (vq->desc_next)
            kfree(vq->desc_next);
        if (vq->ring_phys)
            free_pages(vq->ring_phys, vq->ring_order);
        kfree(vq);
        vpdev->vqs[i] = NULL;
    }
    vpdev->nvqs = 0;
}

/* -------------------- Legacy transport -------------------- */

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
    if (!buf || !len)
        return 0;
    if (!vpdev->ioaddr)
        return -1;
    uint16_t base = (uint16_t)(vpdev->ioaddr + VIRTIO_PCI_CONFIG_OFF(0));
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        p[i] = inb((uint16_t)(base + offset + i));
    return 0;
}

static void vp_notify_legacy(struct virtqueue *vq)
{
    if (!vq || !vq->vdev)
        return;
    struct virtio_pci_device *vpdev = to_vpdev(vq->vdev);
    if (!vpdev || !vpdev->ioaddr)
        return;
    outw((uint16_t)(vpdev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY), vq->index);
}

static int vp_find_vqs_legacy(struct virtio_device *vdev, uint16_t nvqs, struct virtqueue **vqs)
{
    if (!vdev || !vqs || nvqs == 0 || nvqs > 8)
        return -1;
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    if (!vpdev || !vpdev->ioaddr)
        return -1;
    uint16_t ioaddr = vpdev->ioaddr;

    vp_del_vqs(vdev);
    for (uint16_t i = 0; i < nvqs; i++) {
        outw((uint16_t)(ioaddr + VIRTIO_PCI_QUEUE_SEL), i);
        uint16_t qnum = inw((uint16_t)(ioaddr + VIRTIO_PCI_QUEUE_NUM));
        if (qnum == 0) {
            vp_del_vqs(vdev);
            return -1;
        }

        uint32_t ring_size = vring_size(qnum, VIRTIO_PCI_VRING_ALIGN);
        unsigned int order = vp_order_for_size(ring_size);
        void *phys = alloc_pages(GFP_KERNEL, order);
        if (!phys) {
            vp_del_vqs(vdev);
            return -1;
        }
        uint32_t ring_phys = (uint32_t)phys;
        void *ring_virt = memlayout_directmap_phys_to_virt(ring_phys);
        memset(ring_virt, 0, 4096u << order);

        struct virtqueue *vq = (struct virtqueue *)kmalloc(sizeof(*vq));
        if (!vq) {
            free_pages(ring_phys, order);
            vp_del_vqs(vdev);
            return -1;
        }
        memset(vq, 0, sizeof(*vq));
        vq->vdev = vdev;
        vq->index = i;
        vq->num = qnum;
        vq->ring_phys = ring_phys;
        vq->ring_virt = ring_virt;
        vq->ring_order = order;
        vq->notify = vp_notify_legacy;
        vq->notify_addr = NULL;

        vring_init(&vq->vr, qnum, ring_virt, VIRTIO_PCI_VRING_ALIGN);
        vq->desc_next = (uint16_t *)kmalloc(sizeof(uint16_t) * qnum);
        if (!vq->desc_next) {
            kfree(vq);
            free_pages(ring_phys, order);
            vp_del_vqs(vdev);
            return -1;
        }
        for (uint16_t d = 0; d < qnum - 1; d++)
            vq->desc_next[d] = (uint16_t)(d + 1);
        vq->desc_next[qnum - 1] = 0xFFFFu;
        vq->free_head = 0;
        vq->num_free = qnum;

        outl((uint16_t)(ioaddr + VIRTIO_PCI_QUEUE_PFN), (uint32_t)(ring_phys >> VIRTIO_PCI_QUEUE_ADDR_SHIFT));
        vqs[i] = vq;
        vpdev->vqs[i] = vq;
        vpdev->nvqs = (uint16_t)(i + 1);
    }
    return 0;
}

static const struct virtio_config_ops vp_legacy_config_ops = {
    .get_features = vp_get_features,
    .set_features = vp_set_features,
    .get_status = vp_get_status,
    .set_status = vp_set_status,
    .reset = vp_reset,
    .get_config = vp_get_config,
    .find_vqs = vp_find_vqs_legacy,
    .del_vqs = vp_del_vqs,
};

/* -------------------- Modern transport -------------------- */

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
    if (!vpdev || !vpdev->common_cfg)
        return 0;
    volatile struct virtio_pci_common_cfg *cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    cfg->device_feature_select = 0;
    __asm__ volatile ("" ::: "memory");
    uint32_t lo = cfg->device_feature;
    cfg->device_feature_select = 1;
    __asm__ volatile ("" ::: "memory");
    uint32_t hi = cfg->device_feature;
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static void vp_set_features_modern(struct virtio_device *vdev, uint64_t features)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    if (!vpdev || !vpdev->common_cfg)
        return;
    volatile struct virtio_pci_common_cfg *cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    cfg->driver_feature_select = 0;
    cfg->driver_feature = (uint32_t)(features & 0xFFFFFFFFu);
    cfg->driver_feature_select = 1;
    cfg->driver_feature = (uint32_t)(features >> 32);
    __asm__ volatile ("" ::: "memory");
}

static uint8_t vp_get_status_modern(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    if (!vpdev || !vpdev->common_cfg)
        return 0;
    volatile struct virtio_pci_common_cfg *cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
    return cfg->device_status;
}

static void vp_set_status_modern(struct virtio_device *vdev, uint8_t status)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    if (!vpdev || !vpdev->common_cfg)
        return;
    volatile struct virtio_pci_common_cfg *cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;
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
    if (!vpdev || !vpdev->device_cfg || !buf || !len)
        return 0;
    if (offset + len > vpdev->device_cfg_len)
        return -1;
    volatile const uint8_t *src = (volatile const uint8_t *)vpdev->device_cfg;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = src[offset + i];
    return 0;
}

static int vp_find_vqs_modern(struct virtio_device *vdev, uint16_t nvqs, struct virtqueue **vqs)
{
    if (!vdev || !vqs || nvqs == 0 || nvqs > 8)
        return -1;
    struct virtio_pci_device *vpdev = to_vpdev(vdev);
    if (!vpdev || !vpdev->common_cfg || !vpdev->notify_base || vpdev->notify_off_multiplier == 0)
        return -1;
    volatile struct virtio_pci_common_cfg *cfg = (volatile struct virtio_pci_common_cfg *)vpdev->common_cfg;

    vp_del_vqs(vdev);
    for (uint16_t i = 0; i < nvqs; i++) {
        cfg->queue_select = i;
        __asm__ volatile ("" ::: "memory");
        uint16_t qnum = cfg->queue_size;
        if (qnum == 0) {
            vp_del_vqs(vdev);
            return -1;
        }

        uint32_t ring_size = vring_size(qnum, VIRTIO_PCI_VRING_ALIGN);
        unsigned int order = vp_order_for_size(ring_size);
        void *phys = alloc_pages(GFP_KERNEL, order);
        if (!phys) {
            vp_del_vqs(vdev);
            return -1;
        }
        uint32_t ring_phys = (uint32_t)phys;
        void *ring_virt = memlayout_directmap_phys_to_virt(ring_phys);
        memset(ring_virt, 0, 4096u << order);

        struct virtqueue *vq = (struct virtqueue *)kmalloc(sizeof(*vq));
        if (!vq) {
            free_pages(ring_phys, order);
            vp_del_vqs(vdev);
            return -1;
        }
        memset(vq, 0, sizeof(*vq));
        vq->vdev = vdev;
        vq->index = i;
        vq->num = qnum;
        vq->ring_phys = ring_phys;
        vq->ring_virt = ring_virt;
        vq->ring_order = order;
        vq->notify = vp_notify_modern;

        vring_init(&vq->vr, qnum, ring_virt, VIRTIO_PCI_VRING_ALIGN);
        vq->desc_next = (uint16_t *)kmalloc(sizeof(uint16_t) * qnum);
        if (!vq->desc_next) {
            kfree(vq);
            free_pages(ring_phys, order);
            vp_del_vqs(vdev);
            return -1;
        }
        for (uint16_t d = 0; d < qnum - 1; d++)
            vq->desc_next[d] = (uint16_t)(d + 1);
        vq->desc_next[qnum - 1] = 0xFFFFu;
        vq->free_head = 0;
        vq->num_free = qnum;

        uint64_t desc_phys = vp_vq_region_phys(vq, vq->vr.desc);
        uint64_t avail_phys = vp_vq_region_phys(vq, vq->vr.avail);
        uint64_t used_phys = vp_vq_region_phys(vq, vq->vr.used);
        cfg->queue_desc = desc_phys;
        cfg->queue_avail = avail_phys;
        cfg->queue_used = used_phys;
        __asm__ volatile ("" ::: "memory");
        cfg->queue_enable = 1;

        uint16_t notify_off = cfg->queue_notify_off;
        vq->notify_addr = (void *)(vpdev->notify_base + (uint32_t)notify_off * vpdev->notify_off_multiplier);

        vqs[i] = vq;
        vpdev->vqs[i] = vq;
        vpdev->nvqs = (uint16_t)(i + 1);
    }
    return 0;
}

static const struct virtio_config_ops vp_modern_config_ops = {
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
    if (!vpdev || !vpdev->pdev || bar >= 6)
        return NULL;
    uint32_t barv = vpdev->pdev->bar[bar];
    if (barv & 0x1)
        return NULL; /* MMIO only */
    uint32_t base = barv & ~0xFu;
    /*
     * Modern virtio-pci capabilities live in PCI BAR MMIO space, not lowmem RAM.
     * Linux maps these through ioremap(), not the directmap.
     */
    return ioremap(base + offset, 4096);
}

static void vp_parse_modern_caps(struct virtio_pci_device *vpdev)
{
    if (!vpdev || !vpdev->pdev)
        return;
    struct pci_dev *pdev = vpdev->pdev;

    uint8_t cap = 0;
    if (pci_read_config_byte(pdev, 0x34, &cap) != 0 || !cap)
        return;
    while (cap) {
        uint8_t id = 0, next = 0;
        if (pci_read_config_byte(pdev, cap, &id) != 0)
            break;
        if (pci_read_config_byte(pdev, cap + 1, &next) != 0)
            break;

        if (id == PCI_CAP_ID_VNDR) {
            uint8_t cfg_type = 0, bar = 0;
            uint32_t off = 0, len = 0;
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

    if (vpdev->common_cfg && vpdev->notify_base && vpdev->device_cfg && vpdev->notify_off_multiplier)
        vpdev->modern = 1;
}

static int virtio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    (void)id;
    if (!pdev)
        return -1;

    struct virtio_pci_device *vpdev = (struct virtio_pci_device *)kmalloc(sizeof(*vpdev));
    if (!vpdev)
        return -1;
    memset(vpdev, 0, sizeof(*vpdev));
    vpdev->pdev = pdev;
    if (pdev->bar[0] & 0x1)
        vpdev->ioaddr = (uint16_t)(pdev->bar[0] & ~0x3u);
    vp_parse_modern_caps(vpdev);

    static uint32_t virtio_index;
    strcpy(vpdev->name, "virtio");
    char tmp[12];
    itoa((int)virtio_index++, 10, tmp);
    strcat(vpdev->name, tmp);

    device_initialize(&vpdev->vdev.dev, vpdev->name);
    vpdev->vdev.dev.release = virtio_pci_dev_release;
    device_set_parent(&vpdev->vdev.dev, &pdev->dev);
    vpdev->vdev.dev.bus = &virtio_bus_type;
    /* Transport-private pointer (frontends must not depend on it). */
    vpdev->vdev.dev.driver_data = vpdev;

    vpdev->vdev.id.device = VIRTIO_ID_SCSI;
    vpdev->vdev.id.vendor = VIRTIO_DEV_ANY_ID;
    vpdev->vdev.config = vpdev->modern ? &vp_modern_config_ops : &vp_legacy_config_ops;

    /* Start legacy handshake at transport level (device core + drivers will add DRIVER/OK bits). */
    vpdev->vdev.config->reset(&vpdev->vdev);
    vpdev->vdev.config->set_status(&vpdev->vdev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

    pdev->dev.driver_data = vpdev;
    if (register_virtio_device(&vpdev->vdev) != 0) {
        pdev->dev.driver_data = NULL;
        device_unregister(&vpdev->vdev.dev);
        return -1;
    }
    return 0;
}

static void virtio_pci_remove(struct pci_dev *pdev)
{
    if (!pdev)
        return;
    struct virtio_pci_device *vpdev = (struct virtio_pci_device *)pdev->dev.driver_data;
    pdev->dev.driver_data = NULL;
    if (!vpdev)
        return;
    unregister_virtio_device(&vpdev->vdev);
    /* If frontend didn't call del_vqs, transport still reaps allocations. */
    vp_del_vqs(&vpdev->vdev);
    /* vpdev freed by vpdev->vdev.dev.release */
}

static const struct pci_device_id virtio_pci_id_table[] = {
    { .vendor = PCI_VENDOR_ID_QUMRANET, .device = PCI_DEVICE_ID_VIRTIO_SCSI_LEGACY },
    { .vendor = PCI_VENDOR_ID_QUMRANET, .device = PCI_DEVICE_ID_VIRTIO_SCSI_MODERN },
    { 0 }
};

static struct pci_driver virtio_pci_driver = {
    .name = "virtio-pci",
    .id_table = virtio_pci_id_table,
    .probe = virtio_pci_probe,
    .remove = virtio_pci_remove,
};

static int virtio_pci_init(void)
{
    return pci_register_driver(&virtio_pci_driver);
}
module_init(virtio_pci_init);
