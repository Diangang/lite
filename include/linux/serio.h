#ifndef LINUX_SERIO_H
#define LINUX_SERIO_H

#include <stdint.h>
#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/list.h"

#define SERIO_ANY   0xff
#define SERIO_8042  0x01

/*
 * Minimal serio core (Linux mapping):
 * - Port: struct serio (Linux: drivers/input/serio/serio.c)
 * - Driver: struct serio_driver (Linux: drivers/input/serio/serio.c)
 *
 * Lite keeps only the pieces needed for i8042 + atkbd split:
 * - i8042 provides a serio port and calls serio_interrupt() on IRQ1 bytes.
 * - atkbd registers a serio driver and consumes the bytes.
 */

struct serio_device_id {
    uint8_t type;
    uint8_t extra;
    uint8_t id;
    uint8_t proto;
};

struct serio {
    /* Linux mapping: drivers/input/serio/serio.c embeds struct device. */
    struct device dev;
    /*
     * Parent device for the port's struct device (set by provider before
     * serio_register_port()).
     */
    struct device *parent;
    const char *name;
    struct serio_device_id id;
    /*
     * Fast-path cache for IRQ context. driver core owns dev.driver lifetime;
     * we cache the typed pointer on successful bind and clear on unbind.
     */
    struct serio_driver *drv;
};

struct serio_driver {
    const char *description;
    const struct serio_device_id *id_table;
    int (*connect)(struct serio *serio, struct serio_driver *drv);
    void (*disconnect)(struct serio *serio);
    void (*interrupt)(struct serio *serio, uint8_t data);
    struct device_driver driver;
};

int serio_register_port(struct serio *serio);
void serio_unregister_port(struct serio *serio);
int serio_register_driver(struct serio_driver *drv);
void serio_unregister_driver(struct serio_driver *drv);

void serio_interrupt(struct serio *serio, uint8_t data);

#define to_serio_port(d) container_of((d), struct serio, dev)
#define to_serio_driver(d) container_of((d), struct serio_driver, driver)

#endif
