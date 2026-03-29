#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"

static struct device_driver drv_console;
static const struct device_id console_ids[] = {
    { .name = "console", .type = "console" },
    { .name = NULL, .type = NULL }
};

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

    init_driver_ids(&drv_console, "console", platform, console_ids, console_probe);
    driver_register(&drv_console);
    return 0;
}
module_init(console_driver_init);
