#include "linux/device.h"
#include "base.h"
#include "linux/libc.h"

int class_register(struct class *cls)
{
    if (!cls)
        return -1;
    kset_add(device_model_classes_kset(), &cls->kobj);
    return 0;
}

int class_unregister(struct class *cls)
{
    if (!cls)
        return -1;
    kset_remove(device_model_classes_kset(), &cls->kobj);
    kobject_put(&cls->kobj);
    return 0;
}

struct class *class_find(const char *name)
{
    if (!name)
        return NULL;
    struct kobject *cur;
    list_for_each_entry(cur, &device_model_classes_kset()->list, entry) {
        if (!strcmp(cur->name, name))
            return container_of(cur, struct class, kobj);
    }
    return NULL;
}
