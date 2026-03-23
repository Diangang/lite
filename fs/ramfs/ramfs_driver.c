#include "device_model.h"
#include "init.h"
#include "libc.h"

static struct device_driver drv_memfs;

static int memfs_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static int memfs_driver_init(void)
{
    struct bus_type *platform = device_model_platform_bus();
    if (!platform) return -1;

    init_driver(&drv_memfs, "memfs", platform, memfs_probe);
    driver_register(&drv_memfs);
    return 0;
}
module_init(memfs_driver_init);
