#include "linux/tty.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/irqflags.h"
#include "linux/libc.h"
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

const struct device_type tty_dev_type = {
    .name = "tty",
    .devnode = tty_devnode,
};

static uint32_t tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
static uint32_t tty_output_targets = TTY_OUTPUT_SERIAL;

static uint32_t foreground_pid = 0;
static void (*user_exit_hook)(void) = NULL;
static struct class tty_class;
static struct list_head tty_drivers;

extern void n_tty_init(void);
static const struct tty_ldisc_ops *tty_ldisc = &n_tty_ldisc_ops;

int tty_register_driver(struct tty_driver *drv, const char *name, uint32_t num)
{
    if (!drv || !name || num == 0)
        return -1;
    struct tty_driver *cur;
    list_for_each_entry(cur, &tty_drivers, list) {
        if (!strcmp(cur->name, name))
            return -1;
    }
    memset(drv, 0, sizeof(*drv));
    uint32_t len = (uint32_t)strlen(name);
    if (len >= sizeof(drv->name))
        len = sizeof(drv->name) - 1;
    memcpy(drv->name, name, len);
    drv->name[len] = 0;
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
    /*
     * Linux-like: tty line devices are class devices. They may sit under a
     * real parent device in /sys/devices, but do not belong to a bus view.
     */
    struct device *dev = (struct device *)kmalloc(sizeof(*dev));
    if (!dev) {
        kfree(ttydev);
        return NULL;
    }
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, name);
    dev->type = &tty_dev_type;
    dev->class = &tty_class;
    dev->driver_data = ttydev;
    if (parent)
        device_set_parent(dev, parent);
    dev->devt = MKDEV(ttydev->major, ttydev->minor);
    if (device_add(dev) != 0) {
        kobject_put(&dev->kobj);
        kfree(ttydev);
        return NULL;
    }
    return dev;
}

struct tty_port *tty_port_from_dev(struct device *dev)
{
    if (!dev || dev->type != &tty_dev_type)
        return NULL;
    return (struct tty_port *)dev->driver_data;
}

/* tty_init: Initialize TTY. */
void tty_init(void)
{
    tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
    foreground_pid = 0;
    n_tty_init();
}

/* tty_set_user_exit_hook: Implement TTY set user exit hook. */
void tty_set_user_exit_hook(void (*hook)(void))
{
    user_exit_hook = hook;
}

void tty_user_exit_hook_call(void)
{
    if (user_exit_hook)
        user_exit_hook();
}

/* tty_set_foreground_pid: Implement TTY set foreground pid. */
void tty_set_foreground_pid(uint32_t pid)
{
    uint32_t flags = irq_save();
    foreground_pid = pid;
    irq_restore(flags);
}

/* tty_get_foreground_pid: Implement TTY get foreground pid. */
uint32_t tty_get_foreground_pid(void)
{
    uint32_t flags = irq_save();
    uint32_t pid = foreground_pid;
    irq_restore(flags);
    return pid;
}

/* tty_get_flags: Implement TTY get flags. */
uint32_t tty_get_flags(void)
{
    return tty_flags;
}

/* tty_set_output_targets: Implement TTY set output targets. */
void tty_set_output_targets(uint32_t targets)
{
    tty_output_targets |= targets;
}

/* tty_get_output_targets: Implement TTY get output targets. */
uint32_t tty_get_output_targets(void)
{
    return tty_output_targets;
}

/* tty_put_char: Implement TTY put char. */
void tty_put_char(char c)
{
    if (tty_output_targets & TTY_OUTPUT_SERIAL)
        uart_default_put_char(c);
}

/* tty_write: Implement TTY write. */
uint32_t tty_write(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    for (uint32_t i = 0; i < len; i++)
        tty_put_char((char)buf[i]);
    return len;
}

/* tty_set_flags: Implement TTY set flags. */
void tty_set_flags(uint32_t flags)
{
    tty_flags = flags;
}

/* tty_receive_char: Implement TTY receive char. */
void tty_receive_char(char c)
{
    if (tty_ldisc && tty_ldisc->receive_char)
        tty_ldisc->receive_char(c);
}

/* tty_read_blocking: Implement TTY read blocking. */
uint32_t tty_read_blocking(char *buf, uint32_t len)
{
    if (!tty_ldisc || !tty_ldisc->read)
        return 0;
    return tty_ldisc->read(buf, len);
}

static int tty_class_init(void)
{
    memset(&tty_class, 0, sizeof(tty_class));
    tty_class.name = "tty";
    INIT_LIST_HEAD(&tty_class.list);
    INIT_LIST_HEAD(&tty_class.devices);
    INIT_LIST_HEAD(&tty_drivers);
    return class_register(&tty_class);
}
core_initcall(tty_class_init);

static int tty_device_init(void)
{
    if (!tty_class.name)
        return -1;
    /* Linux-like: /dev/tty is a class device (no bus). */
    struct device *dev = (struct device *)kmalloc(sizeof(*dev));
    if (!dev)
        return -1;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, "tty");
    dev->type = &tty_dev_type;
    dev->class = &tty_class;
    /* Provide a stable devtmpfs key: /dev/tty (Linux uses 5:0). */
    dev->devt = MKDEV(5, 0);
    if (device_add(dev) != 0) {
        kobject_put(&dev->kobj);
        return -1;
    }
    return 0;
}
device_initcall(tty_device_init);
