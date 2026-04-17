#ifndef LINUX_SERIO_H
#define LINUX_SERIO_H

#include <stdint.h>
#include "linux/list.h"

/*
 * Minimal serio core (Linux mapping):
 * - Port: struct serio (Linux: drivers/input/serio/serio.c)
 * - Driver: struct serio_driver (Linux: drivers/input/serio/serio.c)
 *
 * Lite keeps only the pieces needed for i8042 + atkbd split:
 * - i8042 provides a serio port and calls serio_interrupt() on IRQ1 bytes.
 * - atkbd registers a serio driver and consumes the bytes.
 */

struct serio_driver;

struct serio {
    const char *name;
    struct list_head node; /* global port list */
    struct serio_driver *drv;
    void *drvdata;
};

struct serio_driver {
    const char *name;
    struct list_head node; /* global driver list */
    int (*connect)(struct serio *serio);
    void (*disconnect)(struct serio *serio);
    void (*interrupt)(struct serio *serio, uint8_t data);
};

void serio_init(void);
int serio_register_port(struct serio *serio);
void serio_unregister_port(struct serio *serio);
int serio_register_driver(struct serio_driver *drv);
void serio_unregister_driver(struct serio_driver *drv);

void serio_interrupt(struct serio *serio, uint8_t data);

#endif
