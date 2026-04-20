#include "linux/device.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "base.h"

static struct device *virtual_root;

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
    if (virtual_root)
        return virtual_root;
    virtual_root = (struct device*)kmalloc(sizeof(struct device));
    if (!virtual_root)
        return NULL;
    memset(virtual_root, 0, sizeof(*virtual_root));
    device_initialize(virtual_root, "virtual");
    if (device_register(virtual_root) != 0) {
        kobject_put(&virtual_root->kobj);
        virtual_root = NULL;
        return NULL;
    }
    return virtual_root;
}

struct device *virtual_root_device(void)
{
    return ensure_virtual_root_device();
}
