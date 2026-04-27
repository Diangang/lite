#include "linux/tty.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/irqflags.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/sched.h"
#include "linux/signal.h"
#include "linux/console.h"
#include "linux/serial_core.h"
#include "linux/tty_ldisc.h"

static const char *tty_devnode(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid)
{
    if (mode)
        *mode = 0666;
    if (uid)
        *uid = 0;
    if (gid)
        *gid = 0;
    return dev ? dev->kobj.name : NULL;
}

static uint32_t tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
static uint32_t tty_output_targets = TTY_OUTPUT_SERIAL;

static uint32_t foreground_pid = 0;
static void (*user_exit_hook)(void) = NULL;
static struct class tty_class;
static struct list_head tty_drivers;

static const struct tty_ldisc_ops *tty_ldisc = &n_tty_ldisc_ops;
static struct tty_struct *tty_active;

static int tty_ldisc_enable(struct tty_struct *tty)
{
    if (!tty || !tty_ldisc || !tty_ldisc->open)
        return 0;
    return tty_ldisc->open(tty);
}

int tty_register_driver(struct tty_driver *drv, const char *name, uint32_t num)
{
    if (!drv || !name || num == 0)
        return -1;
    struct tty_driver *cur;
    list_for_each_entry(cur, &tty_drivers, list) {
        if (!strcmp(cur->name, name))
            return -1;
    }
    uint32_t len = (uint32_t)strlen(name);
    if (len >= sizeof(drv->name))
        len = sizeof(drv->name) - 1;
    memset(drv->name, 0, sizeof(drv->name));
    memcpy(drv->name, name, len);
    drv->num = num;
    INIT_LIST_HEAD(&drv->list);
    list_add_tail(&drv->list, &tty_drivers);
    return 0;
}

struct device *tty_register_device(struct tty_driver *drv, uint32_t index, struct device *parent, const char *name, void *data)
{
    if (!drv || !name || index >= drv->num)
        return NULL;
    if (!tty_class.name)
        return NULL;
    struct tty_port *ttydev = (struct tty_port *)kmalloc(sizeof(*ttydev));
    if (!ttydev)
        return NULL;
    memset(ttydev, 0, sizeof(*ttydev));
    ttydev->driver = drv;
    ttydev->index = index;
    uint32_t len = (uint32_t)strlen(name);
    if (len >= sizeof(ttydev->name))
        len = sizeof(ttydev->name) - 1;
    memcpy(ttydev->name, name, len);
    ttydev->name[len] = 0;
    ttydev->major = 4;
    ttydev->minor = 64 + index;
    ttydev->driver_data = data;
    ttydev->tty.port = ttydev;
    ttydev->tty.disc_data = NULL;

    /* Linux mapping: tty_io.c uses device_create(tty_class, ..., "ttyS%d"). */
    struct device *dev = device_create(&tty_class, parent, MKDEV(ttydev->major, ttydev->minor), ttydev, "%s", name);
    if (!dev) {
        kfree(ttydev);
        return NULL;
    }
    if (tty_ldisc_enable(&ttydev->tty) != 0) {
        device_unregister(dev);
        return NULL;
    }
    if (!tty_active)
        tty_active = &ttydev->tty;
    return dev;
}

void tty_unregister_device(struct tty_driver *drv, uint32_t index)
{
    struct device *dev;
    struct device *n;

    if (!drv)
        return;

    list_for_each_entry_safe(dev, n, &tty_class.devices, class_list) {
        struct tty_port *port;

        if (dev->class != &tty_class)
            continue;
        port = tty_port_from_dev(dev);
        if (!port)
            continue;
        if (port->driver != drv || port->index != index)
            continue;

        if (tty_active == &port->tty)
            tty_active = NULL;
        if (tty_ldisc && tty_ldisc->close)
            tty_ldisc->close(&port->tty);
        device_unregister(dev);
        kfree(port);
        return;
    }
}

struct tty_port *tty_port_from_dev(struct device *dev)
{
    if (!dev || dev->class != &tty_class)
        return NULL;
    return (struct tty_port *)dev->driver_data;
}

void tty_set_user_exit_hook(void (*hook)(void))
{
    user_exit_hook = hook;
}

void tty_user_exit_hook_call(void)
{
    if (user_exit_hook)
        user_exit_hook();
}

void tty_set_foreground_pid(uint32_t pid)
{
    uint32_t flags = irq_save();
    foreground_pid = pid;
    irq_restore(flags);
}

uint32_t tty_get_foreground_pid(void)
{
    uint32_t flags = irq_save();
    uint32_t pid = foreground_pid;
    irq_restore(flags);
    return pid;
}

uint32_t tty_get_flags(void)
{
    return tty_flags;
}

void tty_set_output_targets(uint32_t targets)
{
    tty_output_targets |= targets;
}

uint32_t tty_get_output_targets(void)
{
    return tty_output_targets;
}

void tty_put_char(char c)
{
    if (tty_output_targets & TTY_OUTPUT_SERIAL)
        uart_default_put_char(c);
}

uint32_t tty_write(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    for (uint32_t i = 0; i < len; i++)
        tty_put_char((char)buf[i]);
    return len;
}

void tty_set_flags(uint32_t flags)
{
    tty_flags = flags;
}

void tty_receive_char(char c)
{
    struct tty_struct *tty;

    if (!tty_ldisc)
        return;
    tty = tty_active;
    if (!tty)
        return;
    if (tty_ldisc->receive_buf) {
        uint8_t b = (uint8_t)c;
        tty_ldisc->receive_buf(tty, &b, 1);
        return;
    }
    if (tty_ldisc->receive_char)
        tty_ldisc->receive_char(tty, c);
}

uint32_t tty_read_blocking(char *buf, uint32_t len)
{
    if (!tty_ldisc || !tty_ldisc->read || !tty_active)
        return 0;
    return tty_ldisc->read(tty_active, buf, len);
}

static int tty_class_init(void)
{
    memset(&tty_class, 0, sizeof(tty_class));
    tty_class.name = "tty";
    tty_class.devnode = tty_devnode;
    INIT_LIST_HEAD(&tty_class.list);
    INIT_LIST_HEAD(&tty_class.devices);
    INIT_LIST_HEAD(&tty_drivers);
    return class_register(&tty_class);
}
postcore_initcall(tty_class_init);

/*
 * Linux mapping: drivers/tty/tty_io.c:tty_init()
 * - initialize tty core state
 * - publish the fixed /dev/tty node (5:0)
 */
static int tty_init(void)
{
    if (!tty_class.name)
        return -1;

    tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
    foreground_pid = 0;
    tty_active = NULL;

    if (!device_create(&tty_class, NULL, MKDEV(5, 0), NULL, "tty"))
        return -1;
    return 0;
}
device_initcall(tty_init);
