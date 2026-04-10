#include "linux/pci.h"
#include "linux/init.h"
#include <stdint.h>

/* pcie_scan_device: Implement PCIe scan device. */
int pcie_scan_device(struct pci_dev *pdev)
{
    if (!pdev)
        return 0;
    uint8_t cap = 0;
    if (pci_read_config_byte(pdev, 0x34, &cap) != 0 || !cap)
        return 0;
    int limit = 0;
    while (cap && limit < 48) {
        uint8_t id = 0;
        uint8_t next = 0;
        if (pci_read_config_byte(pdev, cap, &id) != 0)
            return 0;
        if (pci_read_config_byte(pdev, cap + 1, &next) != 0)
            return 0;
        if (id == 0x10) {
            device_uevent_emit("pciecap", &pdev->dev);
            return 1;
        }
        cap = next;
        limit++;
    }
    return 0;
}

/* pcie_init: Initialize PCIe. */
static int pcie_init(void)
{
    return 0;
}
subsys_initcall(pcie_init);
