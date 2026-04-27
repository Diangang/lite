#include "linux/device.h"
#include "linux/init.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/virtio.h"
#include "linux/kernel.h"

/*
 * Virtio core bus glue (minimal subset).
 *
 * Linux mapping:
 *   - drivers/virtio/virtio.c: virtio_dev_match, virtio_dev_probe, driver reg
 *   - include/linux/virtio.h: struct virtio_device/virtio_driver
 */

static int virtio_bus_match(struct device *dev, struct device_driver *drv);
static uint32_t virtio_index_ida;

struct bus_type virtio_bus_type = {
    .name = "virtio",
    .match = virtio_bus_match,
};

void virtio_reset_device(struct virtio_device *vdev)
{
    if (!vdev || !vdev->config || !vdev->config->reset)
        return;
    vdev->config->reset(vdev);
}

void virtio_add_status(struct virtio_device *vdev, uint8_t status)
{
    if (!vdev || !vdev->config || !vdev->config->get_status || !vdev->config->set_status)
        return;
    uint8_t old = vdev->config->get_status(vdev);
    vdev->config->set_status(vdev, (uint8_t)(old | status));
}

int virtio_finalize_features(struct virtio_device *vdev)
{
    if (!vdev || !vdev->config || !vdev->config->get_features || !vdev->config->set_features ||
        !vdev->config->get_status || !vdev->config->set_status)
        return -1;

    /*
     * Linux mapping: virtio_finalize_features() writes negotiated features and sets FEATURES_OK.
     * Lite keeps a minimal negotiation rule: driver requests are intersected with device features.
     */
    uint64_t dev_features = vdev->config->get_features(vdev);
    vdev->features &= dev_features;
    vdev->config->set_features(vdev, vdev->features);

    virtio_add_status(vdev, VIRTIO_CONFIG_S_FEATURES_OK);
    if (!(vdev->config->get_status(vdev) & VIRTIO_CONFIG_S_FEATURES_OK))
        return -1;
    return 0;
}

void virtio_device_ready(struct virtio_device *vdev)
{
    /* Linux mapping: virtio_device_ready() sets DRIVER_OK. */
    virtio_add_status(vdev, VIRTIO_CONFIG_S_DRIVER_OK);
}

void virtio_config_changed(struct virtio_device *vdev)
{
    if (!vdev || !vdev->dev.driver)
        return;
    struct virtio_driver *vdrv = drv_to_virtio(vdev->dev.driver);
    if (vdrv && vdrv->config_changed)
        vdrv->config_changed(vdev);
}

static int virtio_id_match(const struct virtio_device *dev, const struct virtio_device_id *id)
{
    if (!dev || !id)
        return 0;
    if (id->device != dev->id.device && id->device != VIRTIO_DEV_ANY_ID)
        return 0;
    return id->vendor == VIRTIO_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

static int virtio_bus_match(struct device *dev, struct device_driver *drv)
{
    struct virtio_device *vdev = dev_to_virtio(dev);
    struct virtio_driver *vdrv = drv_to_virtio(drv);
    if (!vdev || !vdrv || !vdrv->id_table)
        return 0;
    for (uint32_t i = 0; vdrv->id_table[i].device; i++) {
        if (virtio_id_match(vdev, &vdrv->id_table[i]))
            return 1;
    }
    return 0;
}

static int virtio_driver_probe(struct device *dev)
{
    int ret;
    struct virtio_device *vdev = dev_to_virtio(dev);
    if (!vdev || !vdev->dev.driver)
        return -1;
    struct virtio_driver *vdrv = drv_to_virtio(vdev->dev.driver);
    if (!vdrv || !vdrv->probe)
        return -1;

    virtio_add_status(vdev, VIRTIO_CONFIG_S_DRIVER);
    vdev->features = 0;
    if (virtio_finalize_features(vdev) != 0)
        goto failed;

    ret = vdrv->probe(vdev);
    if (ret != 0)
        goto failed;

    if (vdev->config && vdev->config->get_status &&
        !(vdev->config->get_status(vdev) & VIRTIO_CONFIG_S_DRIVER_OK))
        virtio_device_ready(vdev);
    return 0;

failed:
    virtio_add_status(vdev, VIRTIO_CONFIG_S_FAILED);
    return -1;
}

static void virtio_driver_remove(struct device *dev)
{
    struct virtio_device *vdev = dev_to_virtio(dev);
    if (!vdev || !vdev->dev.driver)
        return;
    struct virtio_driver *vdrv = drv_to_virtio(vdev->dev.driver);
    if (vdrv && vdrv->remove)
        vdrv->remove(vdev);
}

int register_virtio_driver(struct virtio_driver *drv)
{
    if (!drv || !drv->driver.name || !drv->driver.name[0])
        return -1;
    init_driver(&drv->driver, drv->driver.name, &virtio_bus_type, virtio_driver_probe);
    drv->driver.remove = virtio_driver_remove;
    return driver_register(&drv->driver);
}

void unregister_virtio_driver(struct virtio_driver *drv)
{
    if (!drv)
        return;
    driver_unregister(&drv->driver);
}

int register_virtio_device(struct virtio_device *dev)
{
    int ret;

    if (!dev)
        return -1;
    dev->dev.bus = &virtio_bus_type;
    dev->index = virtio_index_ida++;
    snprintf(dev->dev.kobj.name, sizeof(dev->dev.kobj.name), "virtio%u", dev->index);
    virtio_reset_device(dev);
    virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    ret = device_add(&dev->dev);
    if (ret != 0)
        virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
    return ret;
}

void unregister_virtio_device(struct virtio_device *dev)
{
    if (!dev)
        return;
    (void)device_unregister(&dev->dev);
}

static int virtio_init(void)
{
    return bus_register(&virtio_bus_type);
}
core_initcall(virtio_init);
