/*
 * Linux-aligned PCI register and capability IDs.
 *
 * This is a minimal subset used by Lite's PCI/PCIe drivers.
 * Linux: include/uapi/linux/pci_regs.h
 */
#ifndef LINUX_PCI_REGS_H
#define LINUX_PCI_REGS_H

/* Capability IDs (PCI Local Bus Spec) */
#define PCI_CAP_ID_EXP  0x10 /* PCI Express */
#define PCI_CAP_ID_MSIX 0x11 /* MSI-X */

#endif

