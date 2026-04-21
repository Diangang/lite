#include "linux/kobject.h"
#include "linux/init.h"
#include "linux/sysfs.h"
#include "linux/time.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/printk.h"
#include "linux/device.h"
#include "linux/kernel.h"

struct kobject *kernel_kobj;
static struct kobject kernel_kobj_store;

static char kernel_uevent_helper[128] = "";
static uint32_t kernel_attr_show_version(struct kobject *kobj, struct kobj_attribute *attr,
                                         char *buffer, uint32_t cap);
static uint32_t kernel_attr_show_uptime(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buffer, uint32_t cap);
static uint32_t kernel_attr_show_uevent_helper(struct kobject *kobj, struct kobj_attribute *attr,
                                               char *buffer, uint32_t cap);
static uint32_t kernel_attr_store_uevent_helper(struct kobject *kobj, struct kobj_attribute *attr,
                                                const uint8_t *buffer, uint32_t size);
static uint32_t kernel_attr_show_uevent_seqnum(struct kobject *kobj, struct kobj_attribute *attr,
                                               char *buffer, uint32_t cap);

static struct kobj_attribute kernel_attr_version = {
    .attr = { .name = "version", .mode = 0444 },
    .show = kernel_attr_show_version,
    .store = NULL,
};
static struct kobj_attribute kernel_attr_uptime = {
    .attr = { .name = "uptime", .mode = 0444 },
    .show = kernel_attr_show_uptime,
    .store = NULL,
};
static struct kobj_attribute kernel_attr_uevent_helper = {
    .attr = { .name = "uevent_helper", .mode = 0644 },
    .show = kernel_attr_show_uevent_helper,
    .store = kernel_attr_store_uevent_helper,
};
static struct kobj_attribute kernel_attr_uevent_seqnum = {
    .attr = { .name = "uevent_seqnum", .mode = 0444 },
    .show = kernel_attr_show_uevent_seqnum,
    .store = NULL,
};

static const struct attribute *kernel_attrs[] = {
    &kernel_attr_version.attr,
    &kernel_attr_uptime.attr,
    &kernel_attr_uevent_helper.attr,
    &kernel_attr_uevent_seqnum.attr,
    NULL,
};

static const struct attribute_group kernel_attr_group = {
    .name = NULL,
    .attrs = kernel_attrs,
    .is_visible = NULL,
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

static uint32_t kernel_attr_show_version(struct kobject *kobj, struct kobj_attribute *attr,
                                         char *buffer, uint32_t cap)
{
    (void)kobj;
    (void)attr;
    if (!buffer)
        return 0;
    return sysfs_emit_text_line(buffer, cap, "lite-os 0.2");
}

static uint32_t kernel_attr_show_uptime(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buffer, uint32_t cap)
{
    (void)kobj;
    (void)attr;
    if (cap < 2)
        return 0;
    static char tmp[64];
    uint32_t ticks = time_get_jiffies();
    snprintf(tmp, sizeof(tmp), "%u", ticks);
    return sysfs_emit_text_line(buffer, cap, tmp);
}

static uint32_t kernel_attr_show_uevent_helper(struct kobject *kobj, struct kobj_attribute *attr,
                                               char *buffer, uint32_t cap)
{
    (void)kobj;
    (void)attr;
    return sysfs_emit_text_line(buffer, cap, kernel_uevent_helper);
}

static uint32_t kernel_attr_store_uevent_helper(struct kobject *kobj, struct kobj_attribute *attr,
                                                const uint8_t *buffer, uint32_t size)
{
    (void)kobj;
    (void)attr;
    if (!buffer)
        return 0;
    /* Linux mapping: /sys/kernel/uevent_helper is a configurable helper path. */
    uint32_t n = size;
    if (n >= sizeof(kernel_uevent_helper))
        n = sizeof(kernel_uevent_helper) - 1;
    if (n > 0 && buffer[n - 1] == '\n')
        n--;
    memcpy(kernel_uevent_helper, buffer, n);
    kernel_uevent_helper[n] = 0;
    return size;
}

static uint32_t kernel_attr_show_uevent_seqnum(struct kobject *kobj, struct kobj_attribute *attr,
                                               char *buffer, uint32_t cap)
{
    (void)kobj;
    (void)attr;
    if (cap < 2)
        return 0;
    static char tmp[64];
    snprintf(tmp, sizeof(tmp), "%u", device_uevent_seqnum());
    return sysfs_emit_text_line(buffer, cap, tmp);
}

static int ksysfs_init(void)
{
    kobject_init(&kernel_kobj_store, "kernel", NULL);
    kernel_kobj = &kernel_kobj_store;
    /* Selftest: printk returns printed byte count (Linux-like). */
    const char *s = "printk selftest";
    int n = printk(s);
    printk("\n");
    if (n != (int)strlen(s))
        panic("printk return semantics");
    if (kobject_add(kernel_kobj) != 0)
        return -1;
    return sysfs_create_group(kernel_kobj, &kernel_attr_group);
}
core_initcall(ksysfs_init);
