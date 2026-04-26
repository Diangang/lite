#include "linux/pci.h"
#include "linux/string.h"
/*
 * Linux mapping: linux2.6/drivers/pci/pci-sysfs.c
 *
 * Lite keeps a minimal sysfs attribute subset for PCI devices. This file
 * exists primarily to align symbol placement for:
 * - pci_dev_groups[]
 * - pci_dev_type
 */

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

const struct attribute_group *pci_dev_groups[] = {
    &pci_dev_group,
    NULL,
};

struct device_type pci_dev_type = { .name = "pci" };

