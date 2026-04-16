#include "linux/pci.h"
#include "linux/pcie.h"
#include "linux/init.h"
#include "linux/pci_regs.h"
#include <stdint.h>

int pcie_find_capability(struct pci_dev *pdev)
{
    return pci_find_capability(pdev, PCI_CAP_ID_EXP);
}

int pcie_is_pcie(struct pci_dev *pdev)
{
    return pdev && pdev->pcie_cap ? 1 : 0;
}

int pcie_capability_read_word(struct pci_dev *pdev, uint16_t pos, uint16_t *value)
{
    if (!pdev || !value || !pdev->pcie_cap)
        return -1;
    return pci_read_config_word(pdev, (uint8_t)(pdev->pcie_cap + pos), value);
}

int pcie_capability_read_dword(struct pci_dev *pdev, uint16_t pos, uint32_t *value)
{
    if (!pdev || !value || !pdev->pcie_cap)
        return -1;
    return pci_read_config_dword(pdev, (uint8_t)(pdev->pcie_cap + pos), value);
}

int pcie_port_type(struct pci_dev *pdev, uint8_t *type)
{
    if (!type)
        return -1;
    uint16_t flags = 0;
    if (pcie_capability_read_word(pdev, PCI_EXP_FLAGS, &flags) != 0)
        return -1;
    *type = (uint8_t)((flags & PCI_EXP_FLAGS_TYPE) >> PCI_EXP_FLAGS_TYPE_SHIFT);
    return 0;
}

/* pcie_scan_device: Implement PCIe scan device. */
int pcie_scan_device(struct pci_dev *pdev)
{
    if (!pdev)
        return 0;
    int off = pcie_find_capability(pdev);
    pdev->pcie_cap = (uint16_t)off;
    if (off)
        return 1;
    return 0;
}

/* pcie_init: Initialize PCIe. */
static int pcie_init(void)
{
    return 0;
}
subsys_initcall(pcie_init);
