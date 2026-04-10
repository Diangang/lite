#ifndef LINUX_PCIE_H
#define LINUX_PCIE_H

#include "linux/pci.h"

int pcie_scan_device(struct pci_dev *pdev);

#endif
