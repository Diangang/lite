#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/platform_device.h"
#include "linux/errno.h"

static struct class console_class;

static struct device *console_class_dev;

static int console_class_init(void)
{
    memset(&console_class, 0, sizeof(console_class));
    kobject_init(&console_class.kobj, "console", NULL);
    INIT_LIST_HEAD(&console_class.list);
    INIT_LIST_HEAD(&console_class.devices);
    return class_register(&console_class);
}
core_initcall(console_class_init);

static int console_platform_probe(struct platform_device *pdev)
{
    (void)pdev;
    struct class *cls = device_model_console_class();
    if (!cls)
        return -EPROBE_DEFER;

    struct device *parent = device_model_find_device("serial0");
    if (!parent)
        return -EPROBE_DEFER;

    if (console_class_dev)
        return 0;

    /* Class device: no bus (Linux-like). Parent points at the console backend. */
    console_class_dev = device_register_simple_class_parent("console", "console", NULL, cls, parent, NULL);
    if (!console_class_dev)
        return -1;

    /* Provide a stable devtmpfs key: /dev/console (Linux uses 5:1). */
    console_class_dev->dev_major = 5;
    console_class_dev->dev_minor = 1;
    console_class_dev->devnode_name = console_class_dev->kobj.name;
    return 0;
}

static const struct platform_device_id console_platform_ids[] = {
    { .name = "console", .driver_data = 0 },
    { .name = NULL, .driver_data = 0 }
};

static struct platform_driver console_platform_driver = {
    .name = "console",
    .id_table = console_platform_ids,
    .probe = console_platform_probe,
    .remove = NULL,
};

static int console_driver_init(void)
{
    return platform_driver_register(&console_platform_driver);
}
module_init(console_driver_init);
