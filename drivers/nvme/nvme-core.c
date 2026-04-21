#include "linux/init.h"
#include "linux/libc.h"
#include "linux/list.h"
#include "host/nvme.h"

/*
 * NVMe core (Linux mapping): controller object model (/sys/class/nvme/nvmeX).
 * Keep PCI transport glue in `drivers/nvme/nvme.c` for now.
 */

static struct class nvme_class;

static int nvme_class_init(void)
{
    memset(&nvme_class, 0, sizeof(nvme_class));
    nvme_class.name = "nvme";
    INIT_LIST_HEAD(&nvme_class.list);
    INIT_LIST_HEAD(&nvme_class.devices);
    return class_register(&nvme_class);
}
core_initcall(nvme_class_init);

static void nvme_make_ctrl_name(char *name, uint32_t instance)
{
    if (!name)
        return;
    /* Support up to nvme9 in this minimal implementation. */
    name[0] = 'n';
    name[1] = 'v';
    name[2] = 'm';
    name[3] = 'e';
    name[4] = (char)('0' + (instance % 10));
    name[5] = 0;
}

static void nvme_ctrl_release(struct device *dev)
{
    /* Embedded in struct nvme_dev; lifetime is managed by nvme_pci_{probe,remove}. */
    (void)dev;
}

int nvme_ctrl_register(struct nvme_dev *dev)
{
    if (!dev || !dev->pdev)
        return -1;
    if (dev->ctrl_registered)
        return 0;

    char name[16];
    nvme_make_ctrl_name(name, dev->instance);
    device_initialize(&dev->ctrl_dev, name);
    dev->ctrl_dev.release = nvme_ctrl_release;
    /* Avoid global string lookup; nvme_core owns the class object. */
    dev->ctrl_dev.class = &nvme_class;
    device_set_parent(&dev->ctrl_dev, &dev->pdev->dev);
    if (device_add(&dev->ctrl_dev) != 0)
        return -1;
    dev->ctrl_registered = 1;
    return 0;
}

void nvme_ctrl_unregister(struct nvme_dev *dev)
{
    if (!dev || !dev->ctrl_registered)
        return;
    device_unregister(&dev->ctrl_dev);
    dev->ctrl_registered = 0;
}
