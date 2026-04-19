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

uint32_t registered_device_count(void);
struct device *registered_device_at(uint32_t index);
struct device *find_device_by_name(const char *name);
struct kset *devices_kset_get(void);
struct kset *classes_kset_get(void);
struct kset *buses_kset_get(void);
struct kobj_type *ktype_device_get(void);
struct kobj_type *ktype_driver_get(void);
int devtmpfs_init(void);
void devices_init(void);
void buses_init(void);
void classes_init(void);
int platform_bus_init(void);

#endif
