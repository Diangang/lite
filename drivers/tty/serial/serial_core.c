#include "linux/serial_core.h"

#include "linux/device.h"
#include "linux/libc.h"
#include "linux/tty.h"

static struct uart_port *uart_default_port;

int uart_register_driver(struct uart_driver *drv, struct tty_driver *tty_drv)
{
    if (!drv || !tty_drv || !drv->driver_name || !drv->dev_name || drv->nr <= 0)
        return -1;
    drv->tty_drv = tty_drv;
    memset(drv->ports, 0, sizeof(drv->ports));
    return tty_register_driver(drv->tty_drv, drv->driver_name, (uint32_t)drv->nr);
}

static void uart_build_tty_name(char *out, uint32_t cap, const char *prefix, int line)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!prefix) {
        return;
    }
    snprintf(out, cap, "%s%d", prefix, line);
}

int uart_add_one_port(struct uart_driver *drv, struct uart_port *port, struct device *parent)
{
    if (!drv || !drv->tty_drv || !port || !port->ops || !port->ops->put_char)
        return -1;
    if (port->line < 0 || port->line >= drv->nr || port->line >= UART_MAX_PORTS)
        return -1;
    if (drv->ports[port->line])
        return -1;

    port->dev = parent;
    drv->ports[port->line] = port;

    char name[32];
    uart_build_tty_name(name, sizeof(name), drv->dev_name, port->line);
    port->tty_dev = tty_register_device(drv->tty_drv, (uint32_t)port->line, parent, name, NULL);
    if (!port->tty_dev) {
        drv->ports[port->line] = NULL;
        port->dev = NULL;
        return -1;
    }

    /* Lite uses a single global tty output sink. */
    if (!uart_default_port)
        uart_default_port = port;
    return 0;
}

void uart_remove_one_port(struct uart_driver *drv, struct uart_port *port)
{
    if (!drv || !port)
        return;
    if (port->line < 0 || port->line >= UART_MAX_PORTS)
        return;
    if (drv->ports[port->line] != port)
        return;

    if (port->tty_dev) {
        device_unregister(port->tty_dev);
        port->tty_dev = NULL;
    }

    drv->ports[port->line] = NULL;
    port->dev = NULL;

    if (uart_default_port == port)
        uart_default_port = NULL;
}

void uart_write_char(struct uart_port *port, char c)
{
    if (!port || !port->ops || !port->ops->put_char)
        return;
    port->ops->put_char(port, c);
}

void uart_console_write(struct uart_port *port, const uint8_t *buf, uint32_t len)
{
    if (!port || !buf || len == 0)
        return;
    for (uint32_t i = 0; i < len; i++)
        uart_write_char(port, (char)buf[i]);
}

void uart_default_set_port(struct uart_port *port)
{
    uart_default_port = port;
}

void uart_default_put_char(char c)
{
    if (uart_default_port)
        uart_write_char(uart_default_port, c);
}

