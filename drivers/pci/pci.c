#include "linux/pci.h"
#include "linux/pcie.h"
#include "linux/kernel.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/slab.h"

static struct bus_type *pci_bus;
static uint8_t pci_bus_scanned[256];
static struct device *pci_bus_parent[256];
static uint32_t pci_bus_io_base[256];
static uint32_t pci_bus_io_limit[256];
static uint32_t pci_bus_mem_base[256];
static uint32_t pci_bus_mem_limit[256];
static uint64_t pci_bus_pref_base[256];
static uint64_t pci_bus_pref_limit[256];
static uint32_t pci_io_alloc = 0x1000;
static uint64_t pci_mem_alloc = 0x80000000;
static uint64_t pci_pref_alloc = 0x90000000;
static uint8_t pci_next_bus = 1;

static void pci_device_release(struct device *dev)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    kfree(pdev);
}

struct pci_dev *pci_get_pci_dev(struct device *dev)
{
    if (!dev || !dev->type || strcmp(dev->type, "pci"))
        return NULL;
    return container_of(dev, struct pci_dev, dev);
}

/* pci_align32: Implement PCI align32. */
static uint32_t pci_align32(uint32_t val, uint32_t align)
{
    if (!align)
        return val;
    return (val + align - 1) & ~(align - 1);
}

/* pci_align64: Implement PCI align64. */
static uint64_t pci_align64(uint64_t val, uint64_t align)
{
    if (!align)
        return val;
    return (val + align - 1) & ~(align - 1);
}

/* pci_alloc_io: Implement PCI alloc io. */
static uint32_t pci_alloc_io(uint8_t bus, uint32_t size, int *ok)
{
    if (!size || !ok) {
        if (ok)
            *ok = 0;
        return 0;
    }
    if (size & (size - 1)) {
        *ok = 0;
        return 0;
    }
    uint32_t base = pci_io_alloc;
    uint32_t limit = 0;
    if (pci_bus_io_limit[bus] > pci_bus_io_base[bus]) {
        base = pci_bus_io_base[bus];
        limit = pci_bus_io_limit[bus];
    }
    base = pci_align32(base, size);
    if (limit && base + size - 1 > limit) {
        *ok = 0;
        return 0;
    }
    if (limit)
        pci_bus_io_base[bus] = base + size;
    else
        pci_io_alloc = base + size;
    *ok = 1;
    return base;
}

/* pci_alloc_mem64: Implement PCI alloc mem64. */
static uint64_t pci_alloc_mem64(uint8_t bus, uint64_t size, int pref, int *ok)
{
    if (!size || !ok) {
        if (ok)
            *ok = 0;
        return 0;
    }
    if (size & (size - 1)) {
        *ok = 0;
        return 0;
    }
    uint64_t base = pref ? pci_pref_alloc : pci_mem_alloc;
    uint64_t limit = 0;
    if (pref) {
        if (pci_bus_pref_limit[bus] > pci_bus_pref_base[bus]) {
            base = pci_bus_pref_base[bus];
            limit = pci_bus_pref_limit[bus];
        }
    } else {
        if (pci_bus_mem_limit[bus] > pci_bus_mem_base[bus]) {
            base = pci_bus_mem_base[bus];
            limit = pci_bus_mem_limit[bus];
        }
    }
    base = pci_align64(base, size);
    if (limit && base + size - 1 > limit) {
        *ok = 0;
        return 0;
    }
    if (limit) {
        if (pref)
            pci_bus_pref_base[bus] = base + size;
        else
            pci_bus_mem_base[bus] = (uint32_t)(base + size);
    } else {
        if (pref)
            pci_pref_alloc = base + size;
        else
            pci_mem_alloc = base + size;
    }
    *ok = 1;
    return base;
}

/* pci_alloc_mem32: Implement PCI alloc mem32. */
static uint32_t pci_alloc_mem32(uint8_t bus, uint32_t size, int pref, int *ok)
{
    uint64_t base = pci_alloc_mem64(bus, size, pref, ok);
    if (!*ok)
        return 0;
    return (uint32_t)base;
}

/* pci_enable_device: Implement PCI enable device. */
static void pci_enable_device(struct pci_dev *pdev)
{
    if (!pdev)
        return;
    uint16_t cmd = 0;
    if (pci_read_config_word(pdev, 0x04, &cmd) != 0)
        return;
    cmd |= 0x0001;
    cmd |= 0x0002;
    cmd |= 0x0004;
    if (pci_write_config_word(pdev, 0x04, cmd) != 0)
        return;
    device_uevent_emit("enable", &pdev->dev);
}

/* pci_config_addr: Implement PCI config addr. */
static uint32_t pci_config_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    return 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
}

/* pci_config_read32: Implement PCI config read32. */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(0xCF8, pci_config_addr(bus, dev, func, offset));
    return inl(0xCFC);
}

/* pci_config_write32: Implement PCI config write32. */
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    outl(0xCF8, pci_config_addr(bus, dev, func, offset));
    outl(0xCFC, value);
}

/* pci_config_read16: Implement PCI config read16. */
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    uint8_t shift = (offset & 2) * 8;
    return (uint16_t)((val >> shift) & 0xFFFF);
}

/* pci_config_read8: Implement PCI config read8. */
uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    uint8_t shift = (offset & 3) * 8;
    return (uint8_t)((val >> shift) & 0xFF);
}

int pci_read_config_dword(struct pci_dev *pdev, uint8_t offset, uint32_t *value)
{
    if (!pdev || !value)
        return -1;
    *value = pci_config_read32(pdev->bus, (uint8_t)(pdev->devfn >> 3), (uint8_t)(pdev->devfn & 0x7), offset);
    return 0;
}

int pci_read_config_word(struct pci_dev *pdev, uint8_t offset, uint16_t *value)
{
    if (!pdev || !value)
        return -1;
    *value = pci_config_read16(pdev->bus, (uint8_t)(pdev->devfn >> 3), (uint8_t)(pdev->devfn & 0x7), offset);
    return 0;
}

int pci_read_config_byte(struct pci_dev *pdev, uint8_t offset, uint8_t *value)
{
    if (!pdev || !value)
        return -1;
    *value = pci_config_read8(pdev->bus, (uint8_t)(pdev->devfn >> 3), (uint8_t)(pdev->devfn & 0x7), offset);
    return 0;
}

int pci_write_config_word(struct pci_dev *pdev, uint8_t offset, uint16_t value)
{
    if (!pdev)
        return -1;
    uint32_t old = pci_config_read32(pdev->bus, (uint8_t)(pdev->devfn >> 3), (uint8_t)(pdev->devfn & 0x7), offset);
    uint8_t shift = (offset & 2) * 8;
    old &= ~((uint32_t)0xFFFF << shift);
    old |= ((uint32_t)value) << shift;
    pci_config_write32(pdev->bus, (uint8_t)(pdev->devfn >> 3), (uint8_t)(pdev->devfn & 0x7), offset, old);
    return 0;
}

int pci_write_config_dword(struct pci_dev *pdev, uint8_t offset, uint32_t value)
{
    if (!pdev)
        return -1;
    pci_config_write32(pdev->bus, (uint8_t)(pdev->devfn >> 3), (uint8_t)(pdev->devfn & 0x7), offset, value);
    return 0;
}

/* pci_hex: Implement PCI hex. */
static char pci_hex(uint8_t v)
{
    if (v < 10)
        return (char)('0' + v);
    return (char)('a' + (v - 10));
}

/* pci_make_name: Implement PCI make name. */
static void pci_make_name(char *buf, uint8_t bus, uint8_t dev, uint8_t func)
{
    buf[0] = 'p';
    buf[1] = 'c';
    buf[2] = 'i';
    buf[3] = pci_hex((bus >> 4) & 0xF);
    buf[4] = pci_hex(bus & 0xF);
    buf[5] = ':';
    buf[6] = pci_hex((dev >> 4) & 0xF);
    buf[7] = pci_hex(dev & 0xF);
    buf[8] = '.';
    buf[9] = pci_hex(func & 0xF);
    buf[10] = 0;
}

static void pci_scan_bus(uint8_t bus);
/* pci_assign_bridge_bus: Implement PCI assign bridge bus. */
static uint8_t pci_assign_bridge_bus(struct pci_dev *pdev, uint8_t parent_bus)
{
    uint32_t bus_reg = 0;
    if (pci_read_config_dword(pdev, 0x18, &bus_reg) != 0)
        return 0;
    uint8_t secondary = (uint8_t)((bus_reg >> 8) & 0xFF);
    if (secondary == 0 || secondary == parent_bus) {
        if (pci_next_bus == 0)
            return 0;
        secondary = pci_next_bus++;
        uint32_t new_reg = (bus_reg & 0xFF000000) | ((uint32_t)0xFF << 16) | ((uint32_t)secondary << 8) | parent_bus;
        pci_write_config_dword(pdev, 0x18, new_reg);
        device_uevent_emit("busnum", &pdev->dev);
    }
    if (secondary)
        pci_bus_parent[secondary] = &pdev->dev;
    return secondary;
}

/* pci_register_function: Implement PCI register function. */
static void pci_register_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t vendor = pci_config_read16(bus, dev, func, 0x00);
    if (vendor == 0xFFFF)
        return;
    uint16_t device = pci_config_read16(bus, dev, func, 0x02);
    uint32_t class_reg = pci_config_read32(bus, dev, func, 0x08);
    uint8_t class_id = (uint8_t)((class_reg >> 24) & 0xFF);
    uint8_t subclass_id = (uint8_t)((class_reg >> 16) & 0xFF);
    uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
    uint8_t revision = (uint8_t)(class_reg & 0xFF);
    uint8_t header = pci_config_read8(bus, dev, func, 0x0E) & 0x7F;
    char name[16];
    pci_make_name(name, bus, dev, func);
    struct pci_dev *pdev = (struct pci_dev *)kmalloc(sizeof(*pdev));
    if (!pdev)
        return;
    memset(pdev, 0, sizeof(*pdev));
    device_initialize(&pdev->dev, name);
    pdev->dev.release = pci_device_release;
    pdev->dev.type = "pci";
    pdev->dev.bus = pci_bus;

    /* Cached fields (Linux-style naming) for compare/learning. */
    pdev->vendor = vendor;
    pdev->device = device;
    pdev->class = class_id;
    pdev->subclass = subclass_id;
    pdev->prog_if = prog_if;
    pdev->revision = revision;
    pdev->bus = bus;
    pdev->devfn = (uint8_t)((dev << 3) | (func & 0x7));

    if (device_add(&pdev->dev) != 0) {
        kobject_put(&pdev->dev.kobj);
        return;
    }
    if (pci_bus_parent[bus])
        device_set_parent(&pdev->dev, pci_bus_parent[bus]);
    if (class_id == 0x01 && subclass_id == 0x08)
        device_uevent_emit("nvme", &pdev->dev);
    if (header == 0) {
        for (int i = 0; i < 6; i++) {
            uint8_t off = 0x10 + i * 4;
            uint32_t orig = 0;
            pci_read_config_dword(pdev, off, &orig);
            pdev->bar[i] = orig;
            pdev->bar_size[i] = 0;
            if (orig == 0 || orig == 0xFFFFFFFF)
                continue;
            if (orig & 0x1) {
                pci_write_config_dword(pdev, off, 0xFFFFFFFF);
                uint32_t mask = 0;
                pci_read_config_dword(pdev, off, &mask);
                pci_write_config_dword(pdev, off, orig);
                uint32_t size = ~(mask & ~0x3) + 1;
                pdev->bar_size[i] = size;
                if (size) {
                    int ok = 0;
                    uint32_t base = pci_alloc_io(bus, size, &ok);
                    if (ok) {
                        pci_write_config_dword(pdev, off, base | 0x1);
                        pdev->bar[i] = base | 0x1;
                        device_uevent_emit("bar", &pdev->dev);
                    } else {
                        device_uevent_emit("barfail", &pdev->dev);
                    }
                }
                continue;
            }
            uint8_t type = (orig >> 1) & 0x3;
            if (type == 2 && i + 1 < 6) {
                uint32_t orig_high = 0;
                uint32_t mask_low = 0;
                uint32_t mask_high = 0;
                pci_read_config_dword(pdev, off + 4, &orig_high);
                pci_write_config_dword(pdev, off, 0xFFFFFFFF);
                pci_write_config_dword(pdev, off + 4, 0xFFFFFFFF);
                pci_read_config_dword(pdev, off, &mask_low);
                pci_read_config_dword(pdev, off + 4, &mask_high);
                pci_write_config_dword(pdev, off, orig);
                pci_write_config_dword(pdev, off + 4, orig_high);
                uint64_t mask = ((uint64_t)mask_high << 32) | (mask_low & ~0xF);
                uint64_t size64 = (~mask) + 1;
                pdev->bar[i + 1] = orig_high;
                pdev->bar_size[i] = (uint32_t)(size64 & 0xFFFFFFFF);
                pdev->bar_size[i + 1] = (uint32_t)(size64 >> 32);
                if (size64) {
                    int ok = 0;
                    uint64_t base64 = pci_alloc_mem64(bus, size64, (orig & 0x8) != 0, &ok);
                    if (ok) {
                        uint32_t base_low = (uint32_t)(base64 & 0xFFFFFFFF);
                        uint32_t base_high = (uint32_t)(base64 >> 32);
                        pci_write_config_dword(pdev, off, base_low);
                        pci_write_config_dword(pdev, off + 4, base_high);
                        pdev->bar[i] = base_low;
                        pdev->bar[i + 1] = base_high;
                        device_uevent_emit("bar", &pdev->dev);
                    } else {
                        device_uevent_emit("barfail", &pdev->dev);
                    }
                }
                i++;
            } else {
                pci_write_config_dword(pdev, off, 0xFFFFFFFF);
                uint32_t mask = 0;
                pci_read_config_dword(pdev, off, &mask);
                pci_write_config_dword(pdev, off, orig);
                uint32_t size = ~(mask & ~0xF) + 1;
                pdev->bar_size[i] = size;
                if (size) {
                    int ok = 0;
                    uint32_t base = pci_alloc_mem32(bus, size, (orig & 0x8) != 0, &ok);
                    if (ok) {
                        pci_write_config_dword(pdev, off, base);
                        pdev->bar[i] = base;
                        device_uevent_emit("bar", &pdev->dev);
                    } else {
                        device_uevent_emit("barfail", &pdev->dev);
                    }
                }
            }
        }
        pci_enable_device(pdev);
    }
    if (header == 1) {
        uint8_t io_base_lo = pci_config_read8(bus, dev, func, 0x1C);
        uint8_t io_limit_lo = pci_config_read8(bus, dev, func, 0x1D);
        uint16_t mem_base_lo = pci_config_read16(bus, dev, func, 0x20);
        uint16_t mem_limit_lo = pci_config_read16(bus, dev, func, 0x22);
        uint16_t pref_base_lo = pci_config_read16(bus, dev, func, 0x24);
        uint16_t pref_limit_lo = pci_config_read16(bus, dev, func, 0x26);
        pdev->io_base = ((uint32_t)(io_base_lo & 0xF0)) << 8;
        pdev->io_limit = ((uint32_t)(io_limit_lo & 0xF0)) << 8;
        if (io_base_lo & 0x1) {
            uint16_t io_base_hi = pci_config_read16(bus, dev, func, 0x30);
            uint16_t io_limit_hi = pci_config_read16(bus, dev, func, 0x32);
            pdev->io_base |= ((uint32_t)io_base_hi) << 16;
            pdev->io_limit |= ((uint32_t)io_limit_hi) << 16;
        }
        pdev->io_limit |= 0xFFF;
        pdev->mem_base = ((uint32_t)(mem_base_lo & 0xFFF0)) << 16;
        pdev->mem_limit = ((uint32_t)(mem_limit_lo & 0xFFF0)) << 16 | 0xFFFFF;
        uint64_t pref_base = ((uint64_t)(pref_base_lo & 0xFFF0)) << 16;
        uint64_t pref_limit = ((uint64_t)(pref_limit_lo & 0xFFF0)) << 16 | 0xFFFFF;
        if (pref_base_lo & 0x1) {
            uint32_t pref_base_hi = pci_config_read32(bus, dev, func, 0x28);
            uint32_t pref_limit_hi = pci_config_read32(bus, dev, func, 0x2C);
            pref_base |= ((uint64_t)pref_base_hi) << 32;
            pref_limit |= ((uint64_t)pref_limit_hi) << 32;
        }
        pdev->pref_base = pref_base;
        pdev->pref_limit = pref_limit;
        uint8_t secondary = pci_assign_bridge_bus(pdev, bus);
        if (secondary) {
            if (pdev->io_limit > pdev->io_base) {
                pci_bus_io_base[secondary] = pdev->io_base;
                pci_bus_io_limit[secondary] = pdev->io_limit;
            }
            if (pdev->mem_limit > pdev->mem_base) {
                pci_bus_mem_base[secondary] = pdev->mem_base;
                pci_bus_mem_limit[secondary] = pdev->mem_limit;
            }
            if (pdev->pref_limit > pdev->pref_base) {
                pci_bus_pref_base[secondary] = pdev->pref_base;
                pci_bus_pref_limit[secondary] = pdev->pref_limit;
            }
        }
        if (secondary && secondary != bus)
            pci_scan_bus(secondary);
    }
    pcie_scan_device(pdev);
}

/* pci_scan_device: Implement PCI scan device. */
static void pci_scan_device(uint8_t bus, uint8_t dev)
{
    uint16_t vendor = pci_config_read16(bus, dev, 0, 0x00);
    if (vendor == 0xFFFF)
        return;
    uint8_t header = pci_config_read8(bus, dev, 0, 0x0E);
    uint8_t multi = header & 0x80;
    pci_register_function(bus, dev, 0);
    if (!multi)
        return;
    for (uint8_t func = 1; func < 8; func++)
        pci_register_function(bus, dev, func);
}

/* pci_scan_bus: Implement PCI scan bus. */
static void pci_scan_bus(uint8_t bus)
{
    if (pci_bus_scanned[bus])
        return;
    pci_bus_scanned[bus] = 1;
    for (uint8_t dev = 0; dev < 32; dev++)
        pci_scan_device(bus, dev);
}

static int pci_match_one(const struct pci_device_id *id, struct pci_dev *pdev)
{
    if (!id || !pdev)
        return 0;
    if (id->vendor != PCI_ANY_ID && id->vendor != (uint32_t)pdev->vendor)
        return 0;
    if (id->device != PCI_ANY_ID && id->device != (uint32_t)pdev->device)
        return 0;
    if (id->class_mask) {
        uint32_t cls = ((uint32_t)pdev->class << 16) | ((uint32_t)pdev->subclass << 8) | (uint32_t)pdev->prog_if;
        if ((cls & id->class_mask) != (id->class & id->class_mask))
            return 0;
    }
    return 1;
}

static int pci_bus_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    struct pci_dev *pdev = pci_get_pci_dev(dev);
    if (!pdev)
        return 0;
    struct pci_driver *pdrv = to_pci_driver(drv);
    if (!pdrv || !pdrv->id_table)
        return 0;
    const struct pci_device_id *id = pdrv->id_table;
    while (id->vendor || id->device || id->class || id->class_mask) {
        if (pci_match_one(id, pdev))
            return 1;
        id++;
    }
    return 0;
}

static int pci_driver_probe(struct device *dev)
{
    if (!dev || !dev->driver)
        return -1;
    struct pci_dev *pdev = pci_get_pci_dev(dev);
    if (!pdev)
        return -1;
    struct pci_driver *pdrv = container_of(dev->driver, struct pci_driver, driver);
    if (!pdrv || !pdrv->probe || !pdrv->id_table)
        return 0;

    const struct pci_device_id *id = pdrv->id_table;
    while (id->vendor || id->device || id->class || id->class_mask) {
        if (pci_match_one(id, pdev))
            return pdrv->probe(pdev, id);
        id++;
    }
    return 0;
}

static void pci_driver_remove(struct device *dev)
{
    if (!dev || !dev->driver)
        return;
    struct pci_dev *pdev = pci_get_pci_dev(dev);
    if (!pdev)
        return;
    struct pci_driver *pdrv = container_of(dev->driver, struct pci_driver, driver);
    if (pdrv && pdrv->remove)
        pdrv->remove(pdev);
}

int pci_register_driver(struct pci_driver *drv)
{
    if (!drv || !drv->name)
        return -1;
    if (!pci_bus)
        return -1;
    init_driver(&drv->driver, drv->name, pci_bus, pci_driver_probe);
    drv->driver.remove = pci_driver_remove;
    return driver_register(&drv->driver);
}

int pci_unregister_driver(struct pci_driver *drv)
{
    if (!drv)
        return -1;
    return driver_unregister(&drv->driver);
}

/* device_model_pci_bus: Implement device model PCI bus. */
struct bus_type *device_model_pci_bus(void)
{
    return pci_bus;
}

/* pci_init: Initialize PCI. */
static int pci_init(void)
{
    if (pci_bus)
        return 0;
    pci_bus = bus_register("pci", pci_bus_match);
    if (!pci_bus)
        return -1;
    memset(pci_bus_scanned, 0, sizeof(pci_bus_scanned));
    memset(pci_bus_parent, 0, sizeof(pci_bus_parent));
    memset(pci_bus_io_base, 0, sizeof(pci_bus_io_base));
    memset(pci_bus_io_limit, 0, sizeof(pci_bus_io_limit));
    memset(pci_bus_mem_base, 0, sizeof(pci_bus_mem_base));
    memset(pci_bus_mem_limit, 0, sizeof(pci_bus_mem_limit));
    memset(pci_bus_pref_base, 0, sizeof(pci_bus_pref_base));
    memset(pci_bus_pref_limit, 0, sizeof(pci_bus_pref_limit));
    pci_next_bus = 1;
    struct device *root = device_register_simple("pci0000:00", "pci", pci_bus, NULL);
    if (root) {
        device_model_set_pci_root(root);
        pci_bus_parent[0] = root;
    }
    pci_scan_bus(0);
    return 0;
}
subsys_initcall(pci_init);
