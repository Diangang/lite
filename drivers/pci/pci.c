#include "linux/pci.h"
#include "linux/pcie.h"
#include "linux/kernel.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/slab.h"
#include "linux/memlayout.h"
#include "base.h"

struct bus_type pci_bus_type;
static struct device *pci_root_dev;
const struct device_type pci_dev_type = { .name = "pci" };
static const struct device_type pci_bus_dev_type = { .name = "pci_bus" };
static struct class pcibus_class;
static struct pci_bus *pci_buses[256];
static uint8_t pci_next_bus = 1;

static uint64_t pci_align64(uint64_t val, uint64_t align);
static uint32_t pci_align32(uint32_t val, uint32_t align);

static void pci_bus_init_root_windows(struct pci_bus *bus)
{
    if (!bus)
        return;

    /*
     * Linux mapping: host bridge provides IO/MEM/PREFETCH windows.
     * Lite has no host bridge resource discovery yet, so we use the QEMU
     * "PCI hole" defaults, but ensure we never overlap lowmem RAM.
     */
    uint64_t io_start = 0x1000u;
    uint64_t io_end = 0xFFFFu;
    uint64_t mem_end = 0xFEBFFFFFull;
    uint64_t mem_start = 0xE0000000ull;
    uint64_t pref_start = 0xE8000000ull;
    uint64_t pref_end = mem_end;

    uint32_t lowmem_end = memlayout_lowmem_phys_end();
    if (lowmem_end) {
        uint64_t safe_start = pci_align64((uint64_t)lowmem_end + 0x01000000ull, 0x00100000ull);
        if (mem_start < safe_start)
            mem_start = safe_start;
        if (pref_start < safe_start)
            pref_start = safe_start;
    }

    if (mem_start > mem_end)
        mem_start = mem_end = 0;
    if (pref_start > pref_end)
        pref_start = pref_end = 0;

    bus->io_res.name = "PCI I/O";
    bus->io_res.start = io_start;
    bus->io_res.end = io_end;
    bus->io_res.flags = IORESOURCE_IO;
    bus->io_next = (uint32_t)io_start;

    bus->mem_res.name = "PCI MMIO";
    bus->mem_res.start = mem_start;
    bus->mem_res.end = mem_end;
    bus->mem_res.flags = IORESOURCE_MEM;
    bus->mem_next = mem_start;

    bus->pref_res.name = "PCI PREFETCH";
    bus->pref_res.start = pref_start;
    bus->pref_res.end = pref_end;
    bus->pref_res.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;
    bus->pref_next = pref_start;
}

static void pci_bus_set_bridge_windows(struct pci_bus *bus, const struct pci_dev *bridge)
{
    if (!bus || !bridge)
        return;

    if (bridge->io_limit > bridge->io_base) {
        bus->io_res.name = "PCI I/O";
        bus->io_res.start = bridge->io_base;
        bus->io_res.end = bridge->io_limit;
        bus->io_res.flags = IORESOURCE_IO;
        bus->io_next = bridge->io_base;
    }
    if (bridge->mem_limit > bridge->mem_base) {
        bus->mem_res.name = "PCI MMIO";
        bus->mem_res.start = bridge->mem_base;
        bus->mem_res.end = bridge->mem_limit;
        bus->mem_res.flags = IORESOURCE_MEM;
        bus->mem_next = bridge->mem_base;
    }
    if (bridge->pref_limit > bridge->pref_base) {
        bus->pref_res.name = "PCI PREFETCH";
        bus->pref_res.start = bridge->pref_base;
        bus->pref_res.end = bridge->pref_limit;
        bus->pref_res.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;
        bus->pref_next = bridge->pref_base;
    }
}

static void pci_bus_dev_release(struct device *dev)
{
    struct pci_bus *bus = container_of(dev, struct pci_bus, dev);
    kfree(bus);
}

static void pci_make_bus_name(char *buf, uint8_t bus)
{
    static const char hex[] = "0123456789abcdef";
    if (!buf)
        return;
    buf[0] = '0';
    buf[1] = '0';
    buf[2] = '0';
    buf[3] = '0';
    buf[4] = ':';
    buf[5] = hex[(bus >> 4) & 0xF];
    buf[6] = hex[bus & 0xF];
    buf[7] = 0;
}

static struct pci_bus *pci_bus_create(uint8_t busnr, struct device *bridge)
{
    if (pci_buses[busnr])
        return pci_buses[busnr];
    if (!pcibus_class.name || !pcibus_class.name[0])
        return NULL;

    struct pci_bus *bus = (struct pci_bus *)kmalloc(sizeof(*bus));
    if (!bus)
        return NULL;
    memset(bus, 0, sizeof(*bus));
    bus->number = busnr;
    bus->bridge = bridge;
    char name[8];
    pci_make_bus_name(name, busnr);
    device_initialize(&bus->dev, name);
    bus->dev.type = &pci_bus_dev_type;
    bus->dev.class = &pcibus_class;
    bus->dev.release = pci_bus_dev_release;

    if (bus->bridge)
        device_set_parent(&bus->dev, bus->bridge);
    else if (pci_root_device())
        device_set_parent(&bus->dev, pci_root_device());

    if (device_add(&bus->dev) != 0) {
        kobject_put(&bus->dev.kobj);
        return NULL;
    }
    pci_buses[busnr] = bus;
    if (busnr == 0)
        pci_bus_init_root_windows(bus);
    return bus;
}

static int pcibus_class_init(void)
{
    memset(&pcibus_class, 0, sizeof(pcibus_class));
    pcibus_class.name = "pci_bus";
    INIT_LIST_HEAD(&pcibus_class.list);
    INIT_LIST_HEAD(&pcibus_class.devices);
    return class_register(&pcibus_class);
}
core_initcall(pcibus_class_init);

static uint32_t pci_emit_hex_line(char *buffer, uint32_t cap, uint32_t value, uint32_t digits)
{
    static const char hex[] = "0123456789abcdef";
    if (!buffer || cap < digits + 4)
        return 0;
    buffer[0] = '0';
    buffer[1] = 'x';
    for (uint32_t i = 0; i < digits; i++) {
        uint32_t shift = (digits - 1 - i) * 4;
        buffer[2 + i] = hex[(value >> shift) & 0xF];
    }
    buffer[2 + digits] = '\n';
    buffer[3 + digits] = 0;
    return 3 + digits;
}

static uint32_t pci_attr_show_vendor(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    struct pci_dev *pdev = pci_get_pci_dev(dev);
    return pdev ? pci_emit_hex_line(buffer, cap, (uint32_t)pdev->vendor, 4) : 0;
}

static uint32_t pci_attr_show_device(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    struct pci_dev *pdev = pci_get_pci_dev(dev);
    return pdev ? pci_emit_hex_line(buffer, cap, (uint32_t)pdev->device, 4) : 0;
}

static uint32_t pci_attr_show_class(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    struct pci_dev *pdev = pci_get_pci_dev(dev);
    uint32_t cls;
    if (!pdev)
        return 0;
    cls = ((uint32_t)pdev->class << 16) | ((uint32_t)pdev->subclass << 8) | (uint32_t)pdev->prog_if;
    return pci_emit_hex_line(buffer, cap, cls, 6);
}

static struct device_attribute pci_attr_vendor = {
    .attr = { .name = "vendor", .mode = 0444 },
    .show = pci_attr_show_vendor,
};

static struct device_attribute pci_attr_device = {
    .attr = { .name = "device", .mode = 0444 },
    .show = pci_attr_show_device,
};

static struct device_attribute pci_attr_class = {
    .attr = { .name = "class", .mode = 0444 },
    .show = pci_attr_show_class,
};

static uint32_t pci_dev_attr_visible(struct kobject *kobj, const struct attribute *attr)
{
    (void)attr;
    return (kobj && pci_get_pci_dev(container_of(kobj, struct device, kobj))) ? 0444 : 0;
}

static const struct attribute *pci_dev_attrs[] = {
    &pci_attr_vendor.attr,
    &pci_attr_device.attr,
    &pci_attr_class.attr,
    NULL,
};

static const struct attribute_group pci_dev_group = {
    .name = NULL,
    .attrs = pci_dev_attrs,
    .is_visible = pci_dev_attr_visible,
};

static const struct attribute_group *pci_dev_groups[] = {
    &pci_dev_group,
    NULL,
};

static void pci_device_release(struct device *dev)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    kfree(pdev);
}

struct pci_dev *pci_get_pci_dev(struct device *dev)
{
    if (!dev || dev->type != &pci_dev_type)
        return NULL;
    return container_of(dev, struct pci_dev, dev);
}

struct device *pci_root_device(void)
{
    return pci_root_dev;
}

void set_pci_root_device(struct device *dev)
{
    pci_root_dev = dev;
}

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
    struct pci_bus *pb = pci_buses[bus];
    if (!pb || pb->io_res.end <= pb->io_res.start) {
        *ok = 0;
        return 0;
    }
    uint32_t base = pci_align32(pb->io_next, size);
    if ((uint64_t)base + (uint64_t)size - 1 > pb->io_res.end) {
        *ok = 0;
        return 0;
    }
    pb->io_next = base + size;
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
    struct pci_bus *pb = pci_buses[bus];
    struct resource *res = NULL;
    uint64_t *nextp = NULL;
    if (!pb) {
        *ok = 0;
        return 0;
    }
    if (pref) {
        res = &pb->pref_res;
        nextp = &pb->pref_next;
    } else {
        res = &pb->mem_res;
        nextp = &pb->mem_next;
    }
    if (!res || res->end <= res->start) {
        *ok = 0;
        return 0;
    }

    if (*nextp < res->start)
        *nextp = res->start;

    uint64_t base = pci_align64(*nextp, size);
    if (base + size - 1 > res->end) {
        *ok = 0;
        return 0;
    }

    *nextp = base + size;
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

int pci_find_capability(struct pci_dev *pdev, uint8_t cap_id)
{
    if (!pdev)
        return 0;
    uint8_t cap = 0;
    if (pci_read_config_byte(pdev, 0x34, &cap) != 0 || !cap)
        return 0;
    int limit = 0;
    while (cap && limit < 48) {
        uint8_t id = 0;
        uint8_t next = 0;
        if (pci_read_config_byte(pdev, cap, &id) != 0)
            return 0;
        if (pci_read_config_byte(pdev, cap + 1, &next) != 0)
            return 0;
        if (id == cap_id)
            return cap;
        cap = next;
        limit++;
    }
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
    }
    if (secondary) {
        /* Create bus object with bridge parent, similar to Linux sysfs placement. */
        struct pci_bus *child = pci_bus_create(secondary, &pdev->dev);
        if (child)
            child->parent = pci_buses[parent_bus];
    }
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
    pdev->dev.type = &pci_dev_type;
    pdev->dev.bus = &pci_bus_type;

    /* Cached fields (Linux-style naming) for compare/learning. */
    pdev->vendor = vendor;
    pdev->device = device;
    pdev->class = class_id;
    pdev->subclass = subclass_id;
    pdev->prog_if = prog_if;
    pdev->revision = revision;
    pdev->bus = bus;
    pdev->devfn = (uint8_t)((dev << 3) | (func & 0x7));

    struct pci_bus *pb = pci_buses[bus];
    if (pb && pb->bridge)
        device_set_parent(&pdev->dev, pb->bridge);
    else if (pci_root_device())
        device_set_parent(&pdev->dev, pci_root_device());

    if (device_add(&pdev->dev) != 0) {
        kobject_put(&pdev->dev.kobj);
        return;
    }
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
                    } else {
                        printf("pci: BAR IO alloc failed dev=%s size=0x%x\n", pdev->dev.kobj.name, size);
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
                    } else {
                        printf("pci: BAR MEM64 alloc failed dev=%s size=0x%x%08x\n",
                               pdev->dev.kobj.name, (uint32_t)(size64 >> 32), (uint32_t)size64);
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
                    } else {
                        printf("pci: BAR MEM32 alloc failed dev=%s size=0x%x\n", pdev->dev.kobj.name, size);
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
            struct pci_bus *cb = pci_buses[secondary];
            if (cb)
                pci_bus_set_bridge_windows(cb, pdev);
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
    struct pci_bus *pb = pci_buses[bus];
    if (!pb)
        pb = pci_bus_create(bus, NULL);
    if (!pb)
        return;
    if (pb->scanned)
        return;
    pb->scanned = 1;
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
    if (!pci_bus_type.name || !pci_bus_type.name[0])
        return -1;
    init_driver(&drv->driver, drv->name, &pci_bus_type, pci_driver_probe);
    drv->driver.remove = pci_driver_remove;
    return driver_register(&drv->driver);
}

int pci_unregister_driver(struct pci_driver *drv)
{
    if (!drv)
        return -1;
    return driver_unregister(&drv->driver);
}

/* pci_init: Initialize PCI. */
static int pci_init(void)
{
    if (pci_bus_type.name && pci_bus_type.name[0])
        return 0;
    memset(&pci_bus_type, 0, sizeof(pci_bus_type));
    pci_bus_type.name = "pci";
    pci_bus_type.match = pci_bus_match;
    pci_bus_type.dev_groups = pci_dev_groups;
    INIT_LIST_HEAD(&pci_bus_type.list);
    if (bus_register_static(&pci_bus_type) != 0)
        return -1;
    memset(pci_buses, 0, sizeof(pci_buses));
    pci_next_bus = 1;
    struct device *root = (struct device*)kmalloc(sizeof(struct device));
    if (root) {
        memset(root, 0, sizeof(*root));
        device_initialize(root, "pci0000:00");
        root->type = &pci_dev_type;
        root->bus = &pci_bus_type;
        if (device_register(root) == 0) {
            set_pci_root_device(root);
            /* Root bus device will be parented to this root device. */
        } else {
            kobject_put(&root->kobj);
        }
    }
    pci_scan_bus(0);
    return 0;
}
subsys_initcall(pci_init);
