#include "linux/libc.h"
#include "linux/serial_core.h"
#include "linux/console.h"
#include "linux/device.h"
#include "linux/platform_device.h"
#include "linux/tty.h"
#include "linux/init.h"
#include "linux/interrupt.h"
#include "linux/params.h"
#include "base.h"

/*
 * UART 8250/16550A (QEMU COM1 @ 0x3f8, IRQ4)
 *
 * Linux mapping:
 * - Controller driver: drivers/tty/serial/8250/8250_*.c
 * - TTY layer: drivers/tty/tty_io.c
 * - Console selection: console=ttyS0,... and register_console()
 *
 * Lite keeps a minimal subset: one port used as both tty (ttyS0) and optional
 * kernel console sink (console=ttyS0).
 */

static int uart8250_is_transmit_empty(void)
{
    return inb(0x3f8 + 5) & 0x20;
}

static void uart8250_put_char(struct uart_port *port, char c)
{
    (void)port;
    while (uart8250_is_transmit_empty() == 0)
        ;
    outb(0x3f8, c);
}

/* Exported for legacy callers (e.g. early debug paths). */
void serial_put_char(char c)
{
    uart8250_put_char(NULL, c);
}

static const struct uart_ops uart8250_ops = {
    .put_char = uart8250_put_char,
};

static struct uart_port uart8250_port0 = {
    .line = 0,
    .iobase = 0x3f8,
    .irq = IRQ_COM1,
    .ops = &uart8250_ops,
    .dev = NULL,
    .tty_dev = NULL,
};

static int uart8250_console_selected(void)
{
    char console_param[64];
    if (get_cmdline_param("console", console_param, sizeof(console_param)) != 0)
        return 1; /* Default to serial when none specified (Lite/QEMU typical). */
    return !strncmp(console_param, "ttyS0", 5);
}

static void uart8250_console_write(const uint8_t *buf, uint32_t len)
{
    /* Preserve IER and avoid racing RX interrupts while printing. */
    uint8_t saved_ier = (uint8_t)inb(0x3f8 + 1);
    outb(0x3f8 + 1, saved_ier & 0x01);
    uart_console_write(&uart8250_port0, buf, len);
    outb(0x3f8 + 1, saved_ier);
}

static struct console uart8250_console = {
    .name = "ttyS0",
    .write = uart8250_console_write,
    .next = (struct console *)0,
};

static struct tty_driver tty_serial8250_driver;
static struct platform_device *serial8250_pdev;
static struct uart_driver serial8250_uart_driver = {
    .driver_name = "serial8250",
    .dev_name = "ttyS",
    .nr = 1,
    .tty_drv = NULL,
};

static struct pt_regs *uart8250_irq(struct pt_regs *regs)
{
    (void)regs;
    if (inb(0x3f8 + 5) & 1) {
        char c = (char)inb(0x3f8);
        tty_receive_char(c);
    }
    return regs;
}

/* Early console bring-up (polling output, RX IRQ disabled). */
void init_serial(void)
{
    outb(0x3f8 + 1, 0x00);
    outb(0x3f8 + 3, 0x80);
    outb(0x3f8 + 0, 0x03);
    outb(0x3f8 + 1, 0x00);
    outb(0x3f8 + 3, 0x03);
    outb(0x3f8 + 2, 0xC7);

    /* Keep tty output working even before the full driver registers. */
    uart_default_set_port(&uart8250_port0);

    if (uart8250_console_selected())
        register_console(&uart8250_console);
    /* User-visible tty output goes to UART regardless of console=. */
    tty_set_output_targets(TTY_OUTPUT_SERIAL);
}

static int uart8250_probe(struct device *dev)
{
    (void)dev;
    register_irq_handler(IRQ_COM1, uart8250_irq);
    outb(0x3f8 + 4, 0x0B);
    outb(0x3f8 + 1, 0x01);
    printf("8250 UART interrupts enabled.\n");
    return 0;
}

static int serial8250_platform_probe(struct platform_device *pdev)
{
    if (!pdev)
        return -1;
    if (uart8250_probe(&pdev->dev) != 0)
        return -1;
    if (!uart8250_port0.dev)
        return uart_add_one_port(&serial8250_uart_driver, &uart8250_port0, &pdev->dev);
    return 0;
}

static void serial8250_platform_remove(struct platform_device *pdev)
{
    (void)pdev;
    uart_remove_one_port(&serial8250_uart_driver, &uart8250_port0);
}

static const struct platform_device_id serial8250_platform_ids[] = {
    { .name = "serial8250", .driver_data = 0 },
    { .name = NULL, .driver_data = 0 }
};

static struct platform_driver serial8250_platform_driver = {
    .driver = { .name = "serial8250" },
    .id_table = serial8250_platform_ids,
    .probe = serial8250_platform_probe,
    .remove = serial8250_platform_remove,
};

static int serial8250_driver_init(void)
{
    if (uart_register_driver(&serial8250_uart_driver, &tty_serial8250_driver) != 0)
        return -1;
    if (platform_driver_register(&serial8250_platform_driver) != 0)
        return -1;

    /* Minimal enumeration substitute (Linux would use ACPI/PNP/DT on real systems). */
    serial8250_pdev = platform_device_register_simple("serial8250", PLATFORM_DEVID_NONE);
    if (!serial8250_pdev) {
        platform_driver_unregister(&serial8250_platform_driver);
        return -1;
    }
    return 0;
}
module_init(serial8250_driver_init);
