#include "linux/device_model.h"
#include "linux/init.h"
#include "linux/libc.h"

static struct device_driver drv_console;

static int console_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static int console_driver_init(void)
{
    struct bus_type *platform = device_model_platform_bus();
    if (!platform)
        return -1;

    init_driver(&drv_console, "console", platform, console_probe);
    driver_register(&drv_console);
    return 0;
}
module_init(console_driver_init);
