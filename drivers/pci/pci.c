#include "linux/pci.h"
#include "linux/pcie.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/slab.h"

static struct bus_type *pci_bus;
static uint8_t pci_bus_scanned[256];
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

static uint32_t pci_align32(uint32_t val, uint32_t align)
{
    if (!align)
        return val;
    return (val + align - 1) & ~(align - 1);
}

static uint64_t pci_align64(uint64_t val, uint64_t align)
{
    if (!align)
        return val;
    return (val + align - 1) & ~(align - 1);
}

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

static uint32_t pci_alloc_mem32(uint8_t bus, uint32_t size, int pref, int *ok)
{
    uint64_t base = pci_alloc_mem64(bus, size, pref, ok);
    if (!*ok)
        return 0;
    return (uint32_t)base;
}

static void pci_enable_device(struct device *dev)
{
    uint16_t cmd = pci_config_read16_device(dev, 0x04);
    cmd |= 0x0001;
    cmd |= 0x0002;
    cmd |= 0x0004;
    pci_config_write32_device(dev, 0x04, (uint32_t)cmd);
    device_uevent_emit("enable", dev);
}

static uint32_t pci_config_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    return 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(0xCF8, pci_config_addr(bus, dev, func, offset));
    return inl(0xCFC);
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value)
{
    outl(0xCF8, pci_config_addr(bus, dev, func, offset));
    outl(0xCFC, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    uint8_t shift = (offset & 2) * 8;
    return (uint16_t)((val >> shift) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    uint8_t shift = (offset & 3) * 8;
    return (uint8_t)((val >> shift) & 0xFF);
}

uint32_t pci_config_read32_device(struct device *dev, uint8_t offset)
{
    if (!dev)
        return 0xFFFFFFFF;
    return pci_config_read32(dev->bus_num, dev->dev_num, dev->func_num, offset);
}

uint16_t pci_config_read16_device(struct device *dev, uint8_t offset)
{
    if (!dev)
        return 0xFFFF;
    return pci_config_read16(dev->bus_num, dev->dev_num, dev->func_num, offset);
}

uint8_t pci_config_read8_device(struct device *dev, uint8_t offset)
{
    if (!dev)
        return 0xFF;
    return pci_config_read8(dev->bus_num, dev->dev_num, dev->func_num, offset);
}

void pci_config_write32_device(struct device *dev, uint8_t offset, uint32_t value)
{
    if (!dev)
        return;
    pci_config_write32(dev->bus_num, dev->dev_num, dev->func_num, offset, value);
}

static char pci_hex(uint8_t v)
{
    if (v < 10)
        return (char)('0' + v);
    return (char)('a' + (v - 10));
}

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
static uint8_t pci_assign_bridge_bus(struct device *pdev, uint8_t parent_bus)
{
    uint32_t bus_reg = pci_config_read32_device(pdev, 0x18);
    uint8_t secondary = (uint8_t)((bus_reg >> 8) & 0xFF);
    if (secondary == 0 || secondary == parent_bus) {
        if (pci_next_bus == 0)
            return 0;
        secondary = pci_next_bus++;
        uint32_t new_reg = (bus_reg & 0xFF000000) | ((uint32_t)0xFF << 16) | ((uint32_t)secondary << 8) | parent_bus;
        pci_config_write32_device(pdev, 0x18, new_reg);
        device_uevent_emit("busnum", pdev);
    }
    return secondary;
}

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
    struct device *pdev = device_register_simple_full(name, "pci", pci_bus, NULL, NULL, vendor, device, class_id, subclass_id, prog_if, revision, bus, dev, func);
    if (!pdev)
        return;
    if (class_id == 0x01 && subclass_id == 0x08)
        device_uevent_emit("nvme", pdev);
    if (header == 0) {
        for (int i = 0; i < 6; i++) {
            uint8_t off = 0x10 + i * 4;
            uint32_t orig = pci_config_read32_device(pdev, off);
            pdev->bar[i] = orig;
            pdev->bar_size[i] = 0;
            if (orig == 0 || orig == 0xFFFFFFFF)
                continue;
            if (orig & 0x1) {
                pci_config_write32_device(pdev, off, 0xFFFFFFFF);
                uint32_t mask = pci_config_read32_device(pdev, off);
                pci_config_write32_device(pdev, off, orig);
                uint32_t size = ~(mask & ~0x3) + 1;
                pdev->bar_size[i] = size;
                if (size) {
                    int ok = 0;
                    uint32_t base = pci_alloc_io(bus, size, &ok);
                    if (ok) {
                        pci_config_write32_device(pdev, off, base | 0x1);
                        pdev->bar[i] = base | 0x1;
                        device_uevent_emit("bar", pdev);
                    } else {
                        device_uevent_emit("barfail", pdev);
                    }
                }
                continue;
            }
            uint8_t type = (orig >> 1) & 0x3;
            if (type == 2 && i + 1 < 6) {
                uint32_t orig_high = pci_config_read32_device(pdev, off + 4);
                pci_config_write32_device(pdev, off, 0xFFFFFFFF);
                pci_config_write32_device(pdev, off + 4, 0xFFFFFFFF);
                uint32_t mask_low = pci_config_read32_device(pdev, off);
                uint32_t mask_high = pci_config_read32_device(pdev, off + 4);
                pci_config_write32_device(pdev, off, orig);
                pci_config_write32_device(pdev, off + 4, orig_high);
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
                        pci_config_write32_device(pdev, off, base_low);
                        pci_config_write32_device(pdev, off + 4, base_high);
                        pdev->bar[i] = base_low;
                        pdev->bar[i + 1] = base_high;
                        device_uevent_emit("bar", pdev);
                    } else {
                        device_uevent_emit("barfail", pdev);
                    }
                }
                i++;
            } else {
                pci_config_write32_device(pdev, off, 0xFFFFFFFF);
                uint32_t mask = pci_config_read32_device(pdev, off);
                pci_config_write32_device(pdev, off, orig);
                uint32_t size = ~(mask & ~0xF) + 1;
                pdev->bar_size[i] = size;
                if (size) {
                    int ok = 0;
                    uint32_t base = pci_alloc_mem32(bus, size, (orig & 0x8) != 0, &ok);
                    if (ok) {
                        pci_config_write32_device(pdev, off, base);
                        pdev->bar[i] = base;
                        device_uevent_emit("bar", pdev);
                    } else {
                        device_uevent_emit("barfail", pdev);
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

static void pci_scan_bus(uint8_t bus)
{
    if (pci_bus_scanned[bus])
        return;
    pci_bus_scanned[bus] = 1;
    for (uint8_t dev = 0; dev < 32; dev++)
        pci_scan_device(bus, dev);
}

struct bus_type *device_model_pci_bus(void)
{
    return pci_bus;
}

static int pci_init(void)
{
    if (pci_bus)
        return 0;
    pci_bus = bus_register("pci", NULL);
    if (!pci_bus)
        return -1;
    memset(pci_bus_scanned, 0, sizeof(pci_bus_scanned));
    memset(pci_bus_io_base, 0, sizeof(pci_bus_io_base));
    memset(pci_bus_io_limit, 0, sizeof(pci_bus_io_limit));
    memset(pci_bus_mem_base, 0, sizeof(pci_bus_mem_base));
    memset(pci_bus_mem_limit, 0, sizeof(pci_bus_mem_limit));
    memset(pci_bus_pref_base, 0, sizeof(pci_bus_pref_base));
    memset(pci_bus_pref_limit, 0, sizeof(pci_bus_pref_limit));
    pci_next_bus = 1;
    pci_scan_bus(0);
    return 0;
}
subsys_initcall(pci_init);
