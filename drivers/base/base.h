#ifndef DRIVERS_BASE_BASE_H
#define DRIVERS_BASE_BASE_H

#include "linux/device.h"
#include "linux/platform_device.h"

struct device *device_model_platform_root(void);
void device_model_set_platform_root(struct device *dev);
struct device *device_model_pci_root(void);
void device_model_set_pci_root(struct device *dev);
struct device *device_model_virtual_root(void);
struct device *device_model_virtual_subsys(const char *name);

uint32_t device_model_device_count(void);
struct device *device_model_device_at(uint32_t index);
struct device *device_model_find_device(const char *name);
struct kset *device_model_devices_kset(void);
struct kset *device_model_drivers_kset(void);
struct kset *device_model_classes_kset(void);
struct kset *device_model_buses_kset(void);
struct kobj_type *device_model_device_ktype(void);
void device_model_kset_init(void);
int platform_bus_init(void);

#endif
