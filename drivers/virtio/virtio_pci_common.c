#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/page_alloc.h"
#include "linux/slab.h"
#include "linux/virtio_ids.h"
#include "virtio_pci_common.h"

static void virtio_pci_dev_release(struct device *dev)
{
    struct virtio_device *vdev = dev_to_virtio(dev);
    struct virtio_pci_device *vpdev = to_vpdev(vdev);

    kfree(vpdev);
}

void vp_del_vqs(struct virtio_device *vdev)
{
    struct virtio_pci_device *vpdev = to_vpdev(vdev);

    if (!vpdev)
        return;
    for (uint16_t i = 0; i < vpdev->nvqs; i++) {
        struct virtqueue *vq = vpdev->vqs[i];

        if (!vq)
            continue;
        vring_del_virtqueue(vq);
        vpdev->vqs[i] = NULL;
    }
    vpdev->nvqs = 0;
}

static int virtio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct virtio_pci_device *vpdev;

    (void)id;
    if (!pdev)
        return -1;

    vpdev = (struct virtio_pci_device *)kmalloc(sizeof(*vpdev));
    if (!vpdev)
        return -1;
    memset(vpdev, 0, sizeof(*vpdev));
    vpdev->pdev = pdev;
    if (pdev->bar[0] & 0x1)
        vpdev->ioaddr = (uint16_t)(pdev->bar[0] & ~0x3u);
    vp_parse_modern_caps(vpdev);

    device_initialize(&vpdev->vdev.dev, "virtio");
    vpdev->vdev.dev.release = virtio_pci_dev_release;
    device_set_parent(&vpdev->vdev.dev, &pdev->dev);
    vpdev->vdev.dev.bus = &virtio_bus_type;
    vpdev->vdev.dev.driver_data = vpdev;

    vpdev->vdev.id.device = VIRTIO_ID_SCSI;
    vpdev->vdev.id.vendor = VIRTIO_DEV_ANY_ID;
    vpdev->vdev.config = vpdev->modern ? &vp_modern_config_ops : &vp_legacy_config_ops;

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
    struct virtio_pci_device *vpdev;

    if (!pdev)
        return;
    vpdev = (struct virtio_pci_device *)pdev->dev.driver_data;
    pdev->dev.driver_data = NULL;
    if (!vpdev)
        return;
    unregister_virtio_device(&vpdev->vdev);
}

static const struct pci_device_id virtio_pci_id_table[] = {
    { .vendor = PCI_VENDOR_ID_QUMRANET, .device = PCI_DEVICE_ID_VIRTIO_SCSI_LEGACY },
    { .vendor = PCI_VENDOR_ID_QUMRANET, .device = PCI_DEVICE_ID_VIRTIO_SCSI_MODERN },
    { 0 }
};

static struct pci_driver virtio_pci_driver = {
    .driver = { .name = "virtio-pci" },
    .id_table = virtio_pci_id_table,
    .probe = virtio_pci_probe,
    .remove = virtio_pci_remove,
};

static int virtio_pci_init(void)
{
    return pci_register_driver(&virtio_pci_driver);
}
module_init(virtio_pci_init);
