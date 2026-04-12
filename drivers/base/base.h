#ifndef DRIVERS_BASE_BASE_H
#define DRIVERS_BASE_BASE_H

#include "linux/device.h"
#include "linux/platform_device.h"

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
void devices_init(void);
void buses_init(void);
void classes_init(void);
int platform_bus_init(void);

#endif
