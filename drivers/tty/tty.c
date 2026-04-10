#include "linux/tty.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/irqflags.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/sched.h"
#include "linux/wait.h"
#include "linux/signal.h"
#include "linux/console.h"
#include "linux/serial.h"

#define INPUT_BUF_SIZE 256

static char input_buffer[INPUT_BUF_SIZE];
static uint32_t input_head = 0;
static uint32_t input_tail = 0;
static uint32_t input_count = 0;
static wait_queue_t input_waitq;

static uint32_t tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
static char tty_linebuf[256];
static uint32_t tty_line_len = 0;
static uint32_t tty_line_pos = 0;
static uint32_t tty_output_targets = TTY_OUTPUT_SERIAL;

static uint32_t foreground_pid = 0;
static void (*user_exit_hook)(void) = NULL;
static struct class tty_class;
static struct list_head tty_drivers;

static int tty_class_init(void)
{
    memset(&tty_class, 0, sizeof(tty_class));
    kobject_init(&tty_class.kobj, "tty", NULL);
    INIT_LIST_HEAD(&tty_class.list);
    INIT_LIST_HEAD(&tty_class.devices);
    INIT_LIST_HEAD(&tty_drivers);
    return class_register(&tty_class);
}
core_initcall(tty_class_init);

static int tty_device_init(void)
{
    struct class *cls = device_model_tty_class();
    struct device *parent = device_model_virtual_subsys("tty");
    if (!cls || !parent)
        return -1;
    /* Linux-like: /dev/tty is a class device (no bus). */
    struct device *dev = device_register_simple_class_parent("tty", "tty", NULL, cls, parent, NULL);
    if (!dev)
        return -1;
    /* Provide a stable devtmpfs key: /dev/tty (Linux uses 5:0). */
    dev->dev_major = 5;
    dev->dev_minor = 0;
    dev->devnode_name = dev->kobj.name;
    return 0;
}
device_initcall(tty_device_init);

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
    struct class *cls = device_model_tty_class();
    if (!cls)
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
    struct device *dev = device_register_simple_class_parent(name, "tty", NULL, cls, parent, ttydev);
    if (!dev) {
        kfree(ttydev);
        return NULL;
    }
    dev->dev_major = ttydev->major;
    dev->dev_minor = ttydev->minor;
    dev->devnode_name = ttydev->name;
    return dev;
}

struct tty_port *tty_port_from_dev(struct device *dev)
{
    if (!dev || !dev->type || strcmp(dev->type, "tty"))
        return NULL;
    return (struct tty_port *)dev->driver_data;
}

/* tty_init: Initialize TTY. */
void tty_init(void)
{
    input_head = input_tail = input_count = 0;
    tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
    tty_line_len = 0;
    tty_line_pos = 0;
    foreground_pid = 0;
    wait_queue_init(&input_waitq);
}

/* tty_set_user_exit_hook: Implement TTY set user exit hook. */
void tty_set_user_exit_hook(void (*hook)(void))
{
    user_exit_hook = hook;
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
        serial_put_char(c);
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
    tty_line_len = 0;
    tty_line_pos = 0;
}

/* tty_receive_char: Implement TTY receive char. */
void tty_receive_char(char c)
{
    if (c == '\r')
        c = '\n';

    uint32_t flags = irq_save();
    if (input_count < INPUT_BUF_SIZE) {
        input_buffer[input_head] = c;
        input_head = (input_head + 1) % INPUT_BUF_SIZE;
        input_count++;
        wait_queue_wake_all(&input_waitq);
    }
    irq_restore(flags);
}

/* tty_getchar_blocking: Implement TTY getchar blocking. */
static char tty_getchar_blocking(void)
{
    for (;;) {
        uint32_t flags = irq_save();
        if (input_count > 0) {
            char c = input_buffer[input_tail];
            input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
            input_count--;
            irq_restore(flags);
            return c;
        }
        wait_queue_block_locked(&input_waitq);
        irq_restore(flags);
        task_yield();
    }
}

/* tty_handle_ctrl_c: Implement TTY handle ctrl c. */
static int tty_handle_ctrl_c(void)
{
    uint32_t pid = tty_get_foreground_pid();
    if (pid != 0) {
        sys_kill(pid, SIGINT);
        tty_set_foreground_pid(0);
        if (user_exit_hook) user_exit_hook();
    }
    printf("^C\n");
    return 1;
}

/* tty_read_blocking: Implement TTY read blocking. */
uint32_t tty_read_blocking(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    uint32_t read = 0;
    for (;;) {
        if (tty_flags & TTY_FLAG_CANON) {
            if (tty_line_pos < tty_line_len) {
                while (read < len && tty_line_pos < tty_line_len)
                    buf[read++] = tty_linebuf[tty_line_pos++];
                if (tty_line_pos >= tty_line_len) {
                    tty_line_len = 0;
                    tty_line_pos = 0;
                }
                return read;
            }
            tty_line_len = 0;
            tty_line_pos = 0;
            for (;;) {
                char c = tty_getchar_blocking();
                if (c == 0x03) {
                    tty_handle_ctrl_c();
                    return 0;
                }
                if (c == '\r') c = '\n';
                if (c == '\n') {
                    if (tty_line_len + 1 < sizeof(tty_linebuf))
                        tty_linebuf[tty_line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO)
                        tty_put_char('\n');
                    break;
                }
                if (c == '\b' || c == 0x7F) {
                    if (tty_line_len > 0) {
                        tty_line_len--;
                        if (tty_flags & TTY_FLAG_ECHO) {
                            tty_put_char('\b');
                            tty_put_char(' ');
                            tty_put_char('\b');
                        }
                    }
                    continue;
                }
                if (tty_line_len + 1 < sizeof(tty_linebuf)) {
                    tty_linebuf[tty_line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO)
                        tty_put_char(c);
                }
            }
        } else {
            char c = tty_getchar_blocking();
            if (c == 0x03) {
                tty_handle_ctrl_c();
                return 0;
            }
            if (c == '\r') c = '\n';
            buf[read++] = c;
            if (tty_flags & TTY_FLAG_ECHO)
                tty_put_char(c);
            if (read > 0)
                return read;
        }
    }
}
