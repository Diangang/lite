#ifndef DRIVERS_BASE_BASE_H
#define DRIVERS_BASE_BASE_H

#include "linux/device.h"
#include "linux/platform_device.h"

struct subsys_private {
    struct kset devices_kset;
    struct kset drivers_kset;
    struct klist klist_devices;
    struct klist klist_drivers;
    int drivers_autoprobe;
    struct bus_type *bus;
};

static inline struct klist *bus_devices_klist(struct bus_type *bus)
{
    return (bus && bus->p) ? &bus->p->klist_devices : NULL;
}

static inline struct kset *bus_devices_kset(struct bus_type *bus)
{
    return (bus && bus->p) ? &bus->p->devices_kset : NULL;
}

static inline struct kset *bus_drivers_kset(struct bus_type *bus)
{
    return (bus && bus->p) ? &bus->p->drivers_kset : NULL;
}

static inline struct klist *bus_drivers_klist(struct bus_type *bus)
{
    return (bus && bus->p) ? &bus->p->klist_drivers : NULL;
}

static inline struct kobject *bus_devices_kobj(struct bus_type *bus)
{
    struct kset *kset = bus_devices_kset(bus);
    return kset ? &kset->kobj : NULL;
}

static inline struct kobject *bus_drivers_kobj(struct bus_type *bus)
{
    struct kset *kset = bus_drivers_kset(bus);
    return kset ? &kset->kobj : NULL;
}

static inline int bus_drivers_autoprobe(struct bus_type *bus)
{
    return (bus && bus->p) ? bus->p->drivers_autoprobe : 0;
}

static inline void bus_set_drivers_autoprobe(struct bus_type *bus, int enabled)
{
    if (bus && bus->p)
        bus->p->drivers_autoprobe = enabled ? 1 : 0;
}

struct device *pci_root_device(void);
void set_pci_root_device(struct device *dev);
struct device *virtual_root_device(void);
struct device *virtual_child_device(struct device *vroot, const char *name);

/*
 * Linux mapping:
 * - drivers/base/core.c: devices_kset (/sys/devices)
 * - drivers/base/bus.c:  bus_kset     (/sys/bus)
 * - drivers/base/class.c: class_kset  (/sys/class)
 *
 * Lite keeps them as pointers so sysfs can mount after driver_init() without
 * needing extra non-Linux *_get() helper APIs.
 */
extern struct kset *devices_kset;
extern struct kset *bus_kset;
extern struct kset *class_kset;

int devtmpfs_init(void);
void devices_init(void);
void buses_init(void);
void classes_init(void);
int platform_bus_init(void);

#endif
