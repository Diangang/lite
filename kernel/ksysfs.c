#include "linux/ksysfs.h"
#include "linux/init.h"
#include "linux/sysfs.h"
#include "linux/timer.h"
#include "linux/libc.h"
#include "linux/device.h"

struct subsystem kernel_subsys;

static struct attribute kernel_attr_version = { .name = "version", .mode = 0444 };
static struct attribute kernel_attr_uptime = { .name = "uptime", .mode = 0444 };
static struct attribute kernel_attr_uevent = { .name = "uevent", .mode = 0444 };

static const struct attribute *kernel_default_attrs[] = {
    &kernel_attr_version,
    &kernel_attr_uptime,
    &kernel_attr_uevent,
    NULL,
};

static const struct attribute_group kernel_default_group = {
    .name = NULL,
    .attrs = kernel_default_attrs,
    .is_visible = NULL,
};

static const struct attribute_group *kernel_default_groups[] = {
    &kernel_default_group,
    NULL,
};

static uint32_t sysfs_emit_text_line(char *buffer, uint32_t cap, const char *text)
{
    if (!buffer || cap < 2)
        return 0;
    if (!text)
        text = "";
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= cap)
        n = cap - 2;
    memcpy(buffer, text, n);
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static uint32_t kernel_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    (void)kobj;
    if (!attr || !attr->name || !buffer)
        return 0;

    if (!strcmp(attr->name, "version"))
        return sysfs_emit_text_line(buffer, cap, "lite-os 0.2");

    if (!strcmp(attr->name, "uptime")) {
        if (cap < 2)
            return 0;
        static char tmp[64];
        uint32_t ticks = timer_get_ticks();
        itoa((int)ticks, 10, tmp);
        return sysfs_emit_text_line(buffer, cap, tmp);
    }

    if (!strcmp(attr->name, "uevent")) {
        if (cap == 0)
            return 0;
        /* Keep legacy semantics: dump current uevent buffer from offset 0. */
        uint32_t n = device_uevent_read(0, cap - 1, (uint8_t *)buffer);
        if (n < cap)
            buffer[n] = 0;
        return n;
    }

    return 0;
}

static uint32_t kernel_sysfs_store(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)kobj;
    (void)attr;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0;
}

static const struct sysfs_ops kernel_sysfs_ops = {
    .show = kernel_sysfs_show,
    .store = kernel_sysfs_store,
};

static struct kobj_type kernel_ktype = {
    .release = NULL,
    .sysfs_ops = &kernel_sysfs_ops,
    .default_attrs = NULL,
    .default_groups = kernel_default_groups,
};

static int ksysfs_init(void)
{
    kset_init(&kernel_subsys.kset, "kernel");
    kernel_subsys.kset.kobj.ktype = &kernel_ktype;
    return subsystem_register(&kernel_subsys);
}
core_initcall(ksysfs_init);
