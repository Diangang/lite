#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/init.h"
#include "linux/console.h"
#include "linux/tty.h"
#include "base.h"
#include "8250.h"

static int uart8250_console_selected(void)
{
    char console_param[64];
    if (get_cmdline_param("console", console_param, sizeof(console_param)) != 0)
        return 1;
    return !strncmp(console_param, "ttyS0", 5);
}

static void uart8250_console_write(const uint8_t *buf, uint32_t len)
{
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

void init_serial(void)
{
    outb(0x3f8 + 1, 0x00);
    outb(0x3f8 + 3, 0x80);
    outb(0x3f8 + 0, 0x03);
    outb(0x3f8 + 1, 0x00);
    outb(0x3f8 + 3, 0x03);
    outb(0x3f8 + 2, 0xC7);

    uart_default_set_port(&uart8250_port0);
    if (uart8250_console_selected())
        register_console(&uart8250_console);
    tty_set_output_targets(TTY_OUTPUT_SERIAL);
}
