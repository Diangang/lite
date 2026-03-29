#include "linux/pci.h"
#include "linux/init.h"
#include "linux/device.h"
#include <stdint.h>

int pcie_scan_device(struct device *dev)
{
    if (!dev)
        return 0;
    uint16_t status = pci_config_read16_device(dev, 0x06);
    if (!(status & 0x10))
        return 0;
    uint8_t cap = pci_config_read8_device(dev, 0x34);
    int limit = 0;
    while (cap && limit < 48) {
        uint8_t id = pci_config_read8_device(dev, cap);
        uint8_t next = pci_config_read8_device(dev, cap + 1);
        if (id == 0x10) {
            device_uevent_emit("pciecap", dev);
            return 1;
        }
        cap = next;
        limit++;
    }
    return 0;
}

static int pcie_init(void)
{
    return 0;
}
subsys_initcall(pcie_init);
