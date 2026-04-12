#include "linux/device.h"
#include "base.h"
#include "linux/libc.h"

static struct subsystem class_subsys;

static uint32_t class_attr_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    struct class *cls;
    struct class_attribute *cattr;
    if (!kobj || !attr || !buffer)
        return 0;
    cls = container_of(kobj, struct class, subsys.kset.kobj);
    cattr = container_of(attr, struct class_attribute, attr);
    return cattr->show ? cattr->show(cls, cattr, buffer, cap) : 0;
}

static uint32_t class_attr_store(struct kobject *kobj, const struct attribute *attr,
                                 uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    struct class *cls;
    struct class_attribute *cattr;
    if (!kobj || !attr)
        return 0;
    cls = container_of(kobj, struct class, subsys.kset.kobj);
    cattr = container_of(attr, struct class_attribute, attr);
    return cattr->store ? cattr->store(cls, cattr, offset, size, buffer) : 0;
}

static const struct sysfs_ops class_sysfs_ops = {
    .show = class_attr_show,
    .store = class_attr_store,
};

static struct kobj_type ktype_class = {
    .release = NULL,
    .sysfs_ops = &class_sysfs_ops,
    .default_attrs = NULL,
    .default_groups = NULL,
};

int class_create_file(struct class *cls, const struct class_attribute *attr)
{
    if (!cls || !attr)
        return -1;
    return sysfs_create_file(&cls->subsys.kset.kobj, &attr->attr);
}

void class_remove_file(struct class *cls, const struct class_attribute *attr)
{
    if (!cls || !attr)
        return;
    sysfs_remove_file(&cls->subsys.kset.kobj, &attr->attr);
}

static int add_class_attrs(struct class *cls)
{
    int i = 0;

    if (!cls || !cls->class_attrs)
        return 0;

    while (cls->class_attrs[i].attr.name) {
        if (class_create_file(cls, &cls->class_attrs[i]) != 0)
            goto err_remove;
        i++;
    }
    return 0;

err_remove:
    while (--i >= 0)
        class_remove_file(cls, &cls->class_attrs[i]);
    return -1;
}

static void remove_class_attrs(struct class *cls)
{
    int i = 0;

    if (!cls || !cls->class_attrs)
        return;

    while (cls->class_attrs[i].attr.name) {
        class_remove_file(cls, &cls->class_attrs[i]);
        i++;
    }
}

struct kset *classes_kset_get(void)
{
    return &class_subsys.kset;
}

void classes_init(void)
{
    kset_init(&class_subsys.kset, "class");
    class_subsys.kset.kobj.ktype = &ktype_class;
    (void)subsystem_register(&class_subsys);
}

int class_register(struct class *cls)
{
    int ret;

    if (!cls)
        return -1;
    if (!cls->name || !cls->name[0])
        return -1;
    kset_init(&cls->subsys.kset, cls->name);
    cls->subsys.kset.kobj.ktype = &ktype_class;
    cls->subsys.kset.kobj.kset = classes_kset_get();
    if (!cls->devices.next || !cls->devices.prev)
        INIT_LIST_HEAD(&cls->devices);
    if (!cls->list.next || !cls->list.prev)
        INIT_LIST_HEAD(&cls->list);
    ret = subsystem_register(&cls->subsys);
    if (ret)
        return ret;
    ret = add_class_attrs(cls);
    if (ret) {
        subsystem_unregister(&cls->subsys);
        kobject_put(&cls->subsys.kset.kobj);
    }
    return ret;
}

void class_unregister(struct class *cls)
{
    if (!cls)
        return;
    remove_class_attrs(cls);
    subsystem_unregister(&cls->subsys);
    kobject_put(&cls->subsys.kset.kobj);
}

struct class *class_find(const char *name)
{
    if (!name)
        return NULL;
    struct kobject *cur;
    list_for_each_entry(cur, &classes_kset_get()->list, entry) {
        if (!strcmp(cur->name, name))
            return container_of(cur, struct class, subsys.kset.kobj);
    }
    return NULL;
}
