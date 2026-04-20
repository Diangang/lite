#ifndef LINUX_SERIAL_CORE_H
#define LINUX_SERIAL_CORE_H

/*
 * Minimal Linux-like serial_core subset.
 *
 * Linux mapping:
 * - include/linux/serial_core.h: struct uart_driver / uart_port / uart_ops
 * - drivers/tty/serial/serial_core.c: uart_register_driver(), uart_add_one_port()
 *
 * Lite simplification:
 * - Only what is needed for one or a few UART ports (e.g. 8250/16550A).
 * - RX still ultimately feeds the global tty ldisc via tty_receive_char().
 */

#include <stdint.h>

struct device;
struct tty_driver;

struct uart_port;

struct uart_ops {
    void (*put_char)(struct uart_port *port, char c);
};

struct uart_port {
    int line;                 /* ttyS<line> */
    unsigned long iobase;     /* MMIO/PIO base (PIO in Lite) */
    int irq;
    const struct uart_ops *ops;

    /* Parent controller device and derived tty class device */
    struct device *dev;
    struct device *tty_dev;
};

#define UART_MAX_PORTS 4

struct uart_driver {
    const char *driver_name;  /* e.g. "serial8250" */
    const char *dev_name;     /* e.g. "ttyS" */
    int nr;                   /* number of ports supported */

    /* Lite embeds the tty_driver object (Linux allocates one). */
    struct tty_driver *tty_drv;

    struct uart_port *ports[UART_MAX_PORTS];
};

int uart_register_driver(struct uart_driver *drv, struct tty_driver *tty_drv);
int uart_add_one_port(struct uart_driver *drv, struct uart_port *port, struct device *parent);
void uart_remove_one_port(struct uart_driver *drv, struct uart_port *port);

void uart_write_char(struct uart_port *port, char c);
void uart_console_write(struct uart_port *port, const uint8_t *buf, uint32_t len);

/* Single-active-port helper for Lite's minimal tty output path. */
void uart_default_set_port(struct uart_port *port);
void uart_default_put_char(char c);

#endif

