#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/device.h"
#include "linux/platform_device.h"
#include "linux/tty.h"
#include "linux/init.h"
#include "8250.h"

extern int uart8250_probe(struct device *dev);

struct tty_driver tty_serial8250_driver;
static struct platform_device *serial8250_isa_devs;
struct uart_driver serial8250_uart_driver = {
    .driver_name = "serial8250",
    .dev_name = "ttyS",
    .nr = 1,
    .tty_drv = NULL,
};

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

static int serial8250_init(void)
{
    if (uart_register_driver(&serial8250_uart_driver, &tty_serial8250_driver) != 0)
        return -1;
    if (platform_driver_register(&serial8250_platform_driver) != 0)
        return -1;

    serial8250_isa_devs = platform_device_register_simple("serial8250", PLATFORM_DEVID_NONE);
    if (!serial8250_isa_devs) {
        platform_driver_unregister(&serial8250_platform_driver);
        return -1;
    }
    return 0;
}
module_init(serial8250_init);
