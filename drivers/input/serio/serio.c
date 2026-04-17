#include "linux/serio.h"
#include "linux/init.h"
#include "linux/libc.h"

static struct list_head serio_ports = LIST_HEAD_INIT(serio_ports);
static struct list_head serio_drivers = LIST_HEAD_INIT(serio_drivers);

static void serio_try_bind(struct serio *port)
{
    if (!port || port->drv)
        return;
    struct serio_driver *drv;
    list_for_each_entry(drv, &serio_drivers, node) {
        if (!drv->interrupt)
            continue;
        if (drv->connect && drv->connect(port) != 0)
            continue;
        port->drv = drv;
        return;
    }
}

static void serio_try_bind_all(struct serio_driver *drv)
{
    if (!drv)
        return;
    struct serio *port;
    list_for_each_entry(port, &serio_ports, node) {
        if (port->drv)
            continue;
        if (!drv->interrupt)
            continue;
        if (drv->connect && drv->connect(port) != 0)
            continue;
        port->drv = drv;
    }
}

void serio_init(void)
{
    INIT_LIST_HEAD(&serio_ports);
    INIT_LIST_HEAD(&serio_drivers);
}

int serio_register_port(struct serio *serio)
{
    if (!serio)
        return -1;
    INIT_LIST_HEAD(&serio->node);
    serio->drv = NULL;
    serio->drvdata = NULL;
    list_add_tail(&serio->node, &serio_ports);
    serio_try_bind(serio);
    return 0;
}

void serio_unregister_port(struct serio *serio)
{
    if (!serio)
        return;
    if (serio->drv && serio->drv->disconnect)
        serio->drv->disconnect(serio);
    serio->drv = NULL;
    list_del(&serio->node);
    serio->node.next = NULL;
    serio->node.prev = NULL;
}

int serio_register_driver(struct serio_driver *drv)
{
    if (!drv || !drv->name)
        return -1;
    INIT_LIST_HEAD(&drv->node);
    list_add_tail(&drv->node, &serio_drivers);
    serio_try_bind_all(drv);
    return 0;
}

void serio_unregister_driver(struct serio_driver *drv)
{
    if (!drv)
        return;
    struct serio *port;
    list_for_each_entry(port, &serio_ports, node) {
        if (port->drv != drv)
            continue;
        if (drv->disconnect)
            drv->disconnect(port);
        port->drv = NULL;
    }
    list_del(&drv->node);
    drv->node.next = NULL;
    drv->node.prev = NULL;
}

void serio_interrupt(struct serio *serio, uint8_t data)
{
    if (!serio || !serio->drv || !serio->drv->interrupt)
        return;
    serio->drv->interrupt(serio, data);
}

static int serio_core_init(void)
{
    serio_init();
    return 0;
}
module_init(serio_core_init);
