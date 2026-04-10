#ifndef LINUX_PCIE_H
#define LINUX_PCIE_H

#include "linux/pci.h"
#include "linux/pci_regs.h"

/* Scan PCI capabilities and record the PCIe capability offset (if present). */
int pcie_scan_device(struct pci_dev *pdev);
int pcie_find_capability(struct pci_dev *pdev);

#endif
