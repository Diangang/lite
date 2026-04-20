#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/platform_device.h"
#include "linux/errno.h"
#include "linux/slab.h"
#include "base.h"

static struct class console_class;
static struct device *console_class_dev;

static const char *console_devnode(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid)
{
    if (mode)
        *mode = 0600;
    if (uid)
        *uid = 0;
    if (gid)
        *gid = 0;
    return dev ? dev->kobj.name : NULL;
}

const struct device_type console_device_type = {
    .name = "console",
    .devnode = console_devnode,
};

static int console_platform_probe(struct platform_device *pdev)
{
    (void)pdev;
    struct class *cls = class_find("console");
    if (!cls)
        return -EPROBE_DEFER;

    struct device *parent = find_device_by_name("serial0");
    if (!parent)
        return -EPROBE_DEFER;

    if (console_class_dev)
        return 0;

    /* Class device: no bus (Linux-like). Parent points at the console backend. */
    console_class_dev = (struct device *)kmalloc(sizeof(*console_class_dev));
    if (!console_class_dev)
        return -1;
    memset(console_class_dev, 0, sizeof(*console_class_dev));
    device_initialize(console_class_dev, "console");
    console_class_dev->type = &console_device_type;
    console_class_dev->class = cls;
    console_class_dev->devt = MKDEV(5, 1);
    device_set_parent(console_class_dev, parent);
    if (device_add(console_class_dev) != 0) {
        kobject_put(&console_class_dev->kobj);
        console_class_dev = NULL;
        return -1;
    }

    /* Provide a stable devtmpfs key: /dev/console (Linux uses 5:1). */
    return 0;
}

static const struct platform_device_id console_platform_ids[] = {
    { .name = "console", .driver_data = 0 },
    { .name = NULL, .driver_data = 0 }
};

static struct platform_driver console_platform_driver = {
    .driver = { .name = "console" },
    .id_table = console_platform_ids,
    .probe = console_platform_probe,
    .remove = NULL,
};

static int console_driver_init(void)
{
    return platform_driver_register(&console_platform_driver);
}
module_init(console_driver_init);

static int console_class_init(void)
{
    memset(&console_class, 0, sizeof(console_class));
    console_class.name = "console";
    INIT_LIST_HEAD(&console_class.list);
    INIT_LIST_HEAD(&console_class.devices);
    return class_register(&console_class);
}
core_initcall(console_class_init);
