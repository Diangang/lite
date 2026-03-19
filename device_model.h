#ifndef DEVICE_MODEL_H
#define DEVICE_MODEL_H

#include <stdint.h>

typedef struct kobject {
    char name[32];
    struct kobject *parent;
    struct kobject *children;
    struct kobject *next;
    uint32_t refcount;
    void (*release)(struct kobject *kobj);
} kobject_t;

typedef struct bus_type bus_type_t;
typedef struct device_driver device_driver_t;

typedef struct device {
    kobject_t kobj;
    bus_type_t *bus;
    device_driver_t *driver;
    const char *type;
    void *driver_data;
} device_t;

struct bus_type {
    kobject_t kobj;
    device_t *devices;
    device_driver_t *drivers;
    int (*match)(device_t *dev, device_driver_t *drv);
    bus_type_t *next;
};

struct device_driver {
    kobject_t kobj;
    bus_type_t *bus;
    int (*probe)(device_t *dev);
    void (*remove)(device_t *dev);
    device_driver_t *next;
};

void device_model_init(void);
bus_type_t *bus_register(const char *name, int (*match)(device_t *, device_driver_t *));
int driver_register(device_driver_t *drv);
int device_register(device_t *dev);
int device_unbind(device_t *dev);
int device_rebind(device_t *dev);

device_t *device_register_simple(const char *name, const char *type, bus_type_t *bus, void *data);
bus_type_t *device_model_platform_bus(void);

uint32_t device_model_device_count(void);
device_t *device_model_device_at(uint32_t index);
device_t *device_model_find_device(const char *name);

#endif
