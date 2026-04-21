/*
 * Linux-aligned PCI register and capability IDs.
 *
 * This is a minimal subset used by Lite's PCI/PCIe drivers.
 * Linux: include/uapi/linux/pci_regs.h
 */
#ifndef LINUX_PCI_REGS_H
#define LINUX_PCI_REGS_H

/* Capability IDs (PCI Local Bus Spec) */
#define PCI_CAP_ID_VNDR 0x09 /* Vendor-specific */
#define PCI_CAP_ID_EXP  0x10 /* PCI Express */
#define PCI_CAP_ID_MSIX 0x11 /* MSI-X */

/*
 * PCI Express Capability (PCI_CAP_ID_EXP) register offsets.
 * Linux: include/uapi/linux/pci_regs.h (PCI_EXP_*).
 */
#define PCI_EXP_FLAGS   0x02 /* PCI Express Capabilities Register (16-bit) */
#define PCI_EXP_LNKCAP  0x0C /* Link Capabilities Register (32-bit) */
#define PCI_EXP_LNKCTL  0x10 /* Link Control Register (16-bit) */
#define PCI_EXP_LNKSTA  0x12 /* Link Status Register (16-bit) */

/* PCI_EXP_FLAGS bitfields (minimal subset). */
#define PCI_EXP_FLAGS_TYPE 0x00F0 /* Port type mask */
#define PCI_EXP_FLAGS_TYPE_SHIFT 4

/* Linux port type values (minimal subset). */
#define PCI_EXP_TYPE_ENDPOINT     0x0
#define PCI_EXP_TYPE_ROOT_PORT    0x4
#define PCI_EXP_TYPE_UPSTREAM     0x5
#define PCI_EXP_TYPE_DOWNSTREAM   0x6

#endif
