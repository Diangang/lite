#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/device.h"
#include "linux/interrupt.h"
#include "linux/serial_core.h"
#include "linux/tty.h"
#include "base.h"
#include "8250.h"

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

struct uart_port uart8250_port0 = {
    .line = 0,
    .iobase = 0x3f8,
    .irq = IRQ_COM1,
    .ops = &uart8250_ops,
    .dev = NULL,
    .tty_dev = NULL,
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

int uart8250_probe(struct device *dev)
{
    (void)dev;
    register_irq_handler(IRQ_COM1, uart8250_irq);
    outb(0x3f8 + 4, 0x0B);
    outb(0x3f8 + 1, 0x01);
    printf("8250 UART interrupts enabled.\n");
    return 0;
}
