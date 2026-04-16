#ifndef LINUX_PCIE_H
#define LINUX_PCIE_H

#include <stdint.h>
#include "linux/pci.h"
#include "linux/pci_regs.h"

/* Scan PCI capabilities and record the PCIe capability offset (if present). */
int pcie_scan_device(struct pci_dev *pdev);
int pcie_find_capability(struct pci_dev *pdev);

/* Linux-like helpers for PCIe capability access. */
int pcie_is_pcie(struct pci_dev *pdev);
int pcie_capability_read_word(struct pci_dev *pdev, uint16_t pos, uint16_t *value);
int pcie_capability_read_dword(struct pci_dev *pdev, uint16_t pos, uint32_t *value);

/* Convenience queries (minimal subset). */
int pcie_port_type(struct pci_dev *pdev, uint8_t *type);

#endif
