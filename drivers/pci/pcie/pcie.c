#include "linux/pci.h"
#include "linux/pcie.h"
#include "linux/init.h"
#include <stdint.h>

int pcie_find_capability(struct pci_dev *pdev)
{
    return pci_find_capability(pdev, PCI_CAP_ID_EXP);
}

/* pcie_scan_device: Implement PCIe scan device. */
int pcie_scan_device(struct pci_dev *pdev)
{
    if (!pdev)
        return 0;
    int off = pcie_find_capability(pdev);
    pdev->pcie_cap = (uint16_t)off;
    if (off) {
        device_uevent_emit("pciecap", &pdev->dev);
        return 1;
    }
    return 0;
}

/* pcie_init: Initialize PCIe. */
static int pcie_init(void)
{
    return 0;
}
subsys_initcall(pcie_init);
