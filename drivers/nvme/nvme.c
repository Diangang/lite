#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/vmalloc.h"
#include <stdint.h>

static struct device_driver drv_nvme;

static const struct device_id nvme_ids[] = {
    { .type = "pci", .class_id = 0x01, .subclass_id = 0x08 },
    { 0 }
};

static uint64_t nvme_bar_phys(struct device *dev, uint64_t *size_out)
{
    if (!dev || !size_out)
        return 0;
    if (dev->bar[0] & 0x1) {
        *size_out = 0;
        return 0;
    }
    uint64_t size = ((uint64_t)dev->bar_size[1] << 32) | dev->bar_size[0];
    if (!size)
        size = dev->bar_size[0];
    if (!size) {
        *size_out = 0;
        return 0;
    }
    uint64_t low = (uint64_t)(dev->bar[0] & ~0xF);
    uint64_t high = (uint64_t)dev->bar[1];
    *size_out = size;
    return (high << 32) | low;
}

static int nvme_probe(struct device *dev)
{
    if (!dev)
        return -1;
    device_uevent_emit("nvmebind", dev);
    uint64_t size = 0;
    uint64_t phys = nvme_bar_phys(dev, &size);
    if (!phys || !size) {
        device_uevent_emit("nvmebarfail", dev);
        return 0;
    }
    if (phys >> 32) {
        device_uevent_emit("nvme64", dev);
        return 0;
    }
    uint32_t map_size = (size >= 0x1000) ? 0x1000 : (uint32_t)size;
    void *mmio = ioremap((uint32_t)phys, map_size);
    if (!mmio) {
        device_uevent_emit("nvmeiomapfail", dev);
        return 0;
    }
    volatile uint32_t *reg = (volatile uint32_t *)mmio;
    uint64_t cap = ((uint64_t)reg[1] << 32) | reg[0];
    uint32_t vs = reg[2];
    printf("nvme %s cap=0x%08x%08x vs=0x%08x\n",
           dev->kobj.name, (uint32_t)(cap >> 32), (uint32_t)cap, vs);
    iounmap(mmio);
    device_uevent_emit("nvmebar", dev);
    return 0;
}

static int nvme_driver_init(void)
{
    struct bus_type *pci = device_model_pci_bus();
    if (!pci)
        return -1;
    init_driver_ids(&drv_nvme, "nvme", pci, nvme_ids, nvme_probe);
    driver_register(&drv_nvme);
    return 0;
}
module_init(nvme_driver_init);
