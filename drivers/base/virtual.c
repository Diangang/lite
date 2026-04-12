#include "linux/device.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "base.h"

struct child_name_match {
    const char *name;
    struct device *match;
};

static int match_child_name(struct device *child, void *data)
{
    struct child_name_match *match = (struct child_name_match *)data;
    if (!child || !match || !match->name)
        return 0;
    if (strcmp(child->kobj.name, match->name))
        return 0;
    match->match = child;
    return 1;
}

static struct device *find_virtual_root_device(void)
{
    struct device *vroot = find_device_by_name("virtual");
    return (vroot && !vroot->kobj.parent) ? vroot : NULL;
}

struct device *virtual_child_device(struct device *vroot, const char *name)
{
    if (!vroot || !name)
        return NULL;
    struct child_name_match match = {
        .name = name,
        .match = NULL,
    };
    (void)device_for_each_child(vroot, &match, match_child_name);
    return match.match;
}

static struct device *ensure_virtual_root_device(void)
{
    struct device *vroot = find_virtual_root_device();
    if (vroot)
        return vroot;
    vroot = (struct device*)kmalloc(sizeof(struct device));
    if (!vroot)
        return NULL;
    memset(vroot, 0, sizeof(*vroot));
    device_initialize(vroot, "virtual");
    if (device_register(vroot) != 0) {
        kobject_put(&vroot->kobj);
        return NULL;
    }
    return vroot;
}

struct device *virtual_root_device(void)
{
    return ensure_virtual_root_device();
}
