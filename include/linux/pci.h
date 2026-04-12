#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include <stdint.h>
#include <stddef.h>
#include "linux/kernel.h"
#include "linux/device.h"

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
    const char *name;
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
int pci_unregister_driver(struct pci_driver *drv);
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

#endif
