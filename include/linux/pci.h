#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include <stdint.h>
#include <stddef.h>
#include "linux/kernel.h"
#include "linux/device.h"
#include "uapi/linux/pci_regs.h"
#include "linux/resource.h"

/* Linux 2.6 compatible PCI core naming (minimal subset). */
#define PCI_ANY_ID 0xFFFF

struct pci_dev {
    /* Linux pattern: pci_dev embeds struct device as `dev`. */
    struct device dev;

    /* Minimal identity/config/resource state kept on pci_dev, not struct device. */
    uint16_t vendor;
    uint16_t device;
    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t bus;
    uint8_t devfn; /* (dev<<3)|func */
    uint16_t pcie_cap; /* PCIe Capability offset (Linux: pci_dev->pcie_cap) */
    uint32_t bar[6];
    uint32_t bar_size[6];
    uint32_t io_base;
    uint32_t io_limit;
    uint32_t mem_base;
    uint32_t mem_limit;
    uint64_t pref_base;
    uint64_t pref_limit;
};

/*
 * Linux mapping: struct pci_bus represents a PCI bus segment and is associated
 * with a sysfs node under /sys/class/pci_bus/<domain:bus>.
 *
 * Lite keeps a minimal subset to replace ad-hoc per-bus global arrays.
 */
struct pci_bus {
    uint8_t number;
    uint8_t scanned;
    struct pci_bus *parent;      /* Linux: pci_bus->parent */
    struct device *bridge;       /* Linux: pci_bus->self (bridge device) */
    struct resource io_res;
    struct resource mem_res;
    struct resource pref_res;
    uint32_t io_next;
    uint64_t mem_next;
    uint64_t pref_next;

    /* Linux: pci_bus has an embedded struct device for sysfs. */
    struct device dev;
};

struct pci_device_id {
    uint32_t vendor;
    uint32_t device;
    uint32_t subvendor;
    uint32_t subdevice;
    uint32_t class;
    uint32_t class_mask;
    unsigned long driver_data;
};

struct pci_driver {
    const struct pci_device_id *id_table;
    int (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
    void (*remove)(struct pci_dev *pdev);

    struct device_driver driver;
};

extern struct bus_type pci_bus_type;
extern const struct device_type pci_dev_type;

static inline struct pci_driver *to_pci_driver(struct device_driver *drv)
{
    return container_of(drv, struct pci_driver, driver);
}

int pci_register_driver(struct pci_driver *drv);
void pci_unregister_driver(struct pci_driver *drv);
struct pci_dev *pci_get_pci_dev(struct device *dev);

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

/* Linux-style config accessors: bus-specific helpers take struct pci_dev *. */
int pci_read_config_byte(struct pci_dev *pdev, uint8_t offset, uint8_t *value);
int pci_read_config_word(struct pci_dev *pdev, uint8_t offset, uint16_t *value);
int pci_read_config_dword(struct pci_dev *pdev, uint8_t offset, uint32_t *value);
int pci_write_config_word(struct pci_dev *pdev, uint8_t offset, uint16_t value);
int pci_write_config_dword(struct pci_dev *pdev, uint8_t offset, uint32_t value);

/* Linux: pci_find_capability() */
int pci_find_capability(struct pci_dev *pdev, uint8_t cap_id);

/*
 * Linux mapping: PCIe capability helpers are in linux2.6/include/linux/pci.h
 * (not a standalone drivers/pci/pcie/pcie.c file).
 */
static inline int pcie_find_capability(struct pci_dev *pdev)
{
    return pci_find_capability(pdev, PCI_CAP_ID_EXP);
}

static inline int pcie_is_pcie(struct pci_dev *pdev)
{
    return (pdev && pdev->pcie_cap) ? 1 : 0;
}

static inline int pcie_capability_read_word(struct pci_dev *pdev, uint16_t pos, uint16_t *value)
{
    if (!pdev || !value || !pdev->pcie_cap)
        return -1;
    return pci_read_config_word(pdev, (uint8_t)(pdev->pcie_cap + pos), value);
}

static inline int pcie_capability_read_dword(struct pci_dev *pdev, uint16_t pos, uint32_t *value)
{
    if (!pdev || !value || !pdev->pcie_cap)
        return -1;
    return pci_read_config_dword(pdev, (uint8_t)(pdev->pcie_cap + pos), value);
}

static inline int pcie_port_type(struct pci_dev *pdev, uint8_t *type)
{
    if (!type)
        return -1;
    uint16_t flags = 0;
    if (pcie_capability_read_word(pdev, PCI_EXP_FLAGS, &flags) != 0)
        return -1;
    *type = (uint8_t)((flags & PCI_EXP_FLAGS_TYPE) >> PCI_EXP_FLAGS_TYPE_SHIFT);
    return 0;
}

static inline int pcie_scan_device(struct pci_dev *pdev)
{
    if (!pdev)
        return 0;
    int off = pcie_find_capability(pdev);
    pdev->pcie_cap = (uint16_t)off;
    return off ? 1 : 0;
}

#endif
