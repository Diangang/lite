#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/timer.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/tty.h"
#include "linux/blkdev.h"
#include "linux/kernel.h"
#include "linux/kobject.h"

static struct dirent sys_dirent;
// Note: sys_root is dynamically allocated in init_sysfs now
static struct inode sys_devices;
static struct inode sys_bus;
static struct inode sys_class;

static struct kobject kernel_kobj;


static void sysfs_init_inode(struct inode *inode, uint32_t flags, uint32_t ino,
                             uint32_t size, struct file_operations *f_ops,
                             uint32_t impl, uint32_t mode);

/* Linux-like internal sysfs node cache. */
struct sysfs_dirent {
    struct inode inode;
    struct kobject *kobj;                 /* Directory owner */
    const struct attribute *attr;         /* File attribute (NULL for directories) */
    const char *name;                     /* Subdir name (NULL for attribute files) */
    struct sysfs_dirent *children;        /* Cached attribute files under a kobj dir */
    struct sysfs_dirent *next;
};

static uint32_t sysfs_next_ino = 0xA000;
static uint32_t sysfs_alloc_ino(void)
{
    return sysfs_next_ino++;
}

static struct file_operations sys_kobj_dir_ops;
static struct file_operations sys_kobj_attr_ops;

static uint32_t sysfs_kobj_attr_mode(struct kobject *kobj, const struct attribute *attr)
{
    if (!kobj || !attr)
        return 0;
    if (!kobj->ktype)
        return attr->mode;

    if (kobj->ktype->default_groups) {
        const struct attribute_group **g = kobj->ktype->default_groups;
        while (g && *g) {
            const struct attribute_group *grp = *g;
            if (grp && grp->attrs) {
                const struct attribute **a = grp->attrs;
                while (*a) {
                    if (*a == attr) {
                        if (grp->is_visible)
                            return grp->is_visible(kobj, attr);
                        return attr->mode;
                    }
                    a++;
                }
            }
            g++;
        }
    }

    if (kobj->ktype->default_attrs) {
        const struct attribute **a = kobj->ktype->default_attrs;
        while (a && *a) {
            if (*a == attr)
                return attr->mode;
            a++;
        }
    }
    return attr->mode;
}

static struct sysfs_dirent *sysfs_get_kobj_sd(struct kobject *kobj)
{
    if (!kobj)
        return NULL;
    if (kobj->sd)
        return kobj->sd;
    struct sysfs_dirent *sd = (struct sysfs_dirent *)kmalloc(sizeof(*sd));
    if (!sd)
        return NULL;
    memset(sd, 0, sizeof(*sd));
    sd->kobj = kobj;
    sysfs_init_inode(&sd->inode, FS_DIRECTORY, sysfs_alloc_ino(), 0, &sys_kobj_dir_ops,
                     (uint32_t)(uintptr_t)kobj, 0555);
    kobj->sd = sd;
    return sd;
}

static struct inode *sysfs_get_kobj_dir_inode(struct kobject *kobj)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    return sd ? &sd->inode : NULL;
}

static struct inode *sysfs_get_kobj_attr_inode(struct kobject *kobj, const struct attribute *attr)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !attr || !attr->name)
        return NULL;

    struct sysfs_dirent *cur = sd->children;
    while (cur) {
        if (cur->attr == attr)
            return &cur->inode;
        cur = cur->next;
    }

    uint32_t mode = sysfs_kobj_attr_mode(kobj, attr);
    if (!mode)
        return NULL;

    /* Keep legacy-compatible sizing for long sysfs text files (e.g. /sys/kernel/uevent). */
    uint32_t size = 256;
    if (!strcmp(attr->name, "uevent"))
        size = 4096;

    struct sysfs_dirent *nd = (struct sysfs_dirent *)kmalloc(sizeof(*nd));
    if (!nd)
        return NULL;
    memset(nd, 0, sizeof(*nd));
    nd->kobj = kobj;
    nd->attr = attr;
    nd->name = NULL;
    sysfs_init_inode(&nd->inode, FS_FILE, sysfs_alloc_ino(), size, &sys_kobj_attr_ops,
                     (uint32_t)(uintptr_t)kobj, mode);
    nd->inode.private_data = (void *)attr;
    nd->next = sd->children;
    sd->children = nd;
    return &nd->inode;
}

static struct inode *sysfs_get_kobj_subdir_inode(struct kobject *kobj, const char *name,
                                                 struct file_operations *f_ops, uint32_t mode)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !name || !name[0] || !f_ops)
        return NULL;

    struct sysfs_dirent *cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name && !strcmp(cur->name, name))
            return &cur->inode;
        cur = cur->next;
    }

    struct sysfs_dirent *nd = (struct sysfs_dirent *)kmalloc(sizeof(*nd));
    if (!nd)
        return NULL;
    memset(nd, 0, sizeof(*nd));
    nd->kobj = kobj;
    nd->attr = NULL;
    nd->name = name;
    sysfs_init_inode(&nd->inode, FS_DIRECTORY, sysfs_alloc_ino(), 0, f_ops, (uint32_t)(uintptr_t)kobj, mode);
    nd->next = sd->children;
    sd->children = nd;
    return &nd->inode;
}

static uint32_t sys_read_symlink(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || !node->private_data)
        return 0;
    const char *target = (const char *)node->private_data;
    uint32_t n = (uint32_t)strlen(target);
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, target + offset, size);
    return size;
}

static struct file_operations sys_symlink_ops = {
    .read = sys_read_symlink,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct inode *sysfs_get_kobj_link_inode(struct kobject *kobj, const char *name, const char *target)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !name || !name[0] || !target || !target[0])
        return NULL;

    struct sysfs_dirent *cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name && cur->inode.flags == FS_SYMLINK && !strcmp(cur->name, name))
            return &cur->inode;
        cur = cur->next;
    }

    uint32_t n = (uint32_t)strlen(target);
    char *copy = (char *)kmalloc(n + 1);
    if (!copy)
        return NULL;
    memcpy(copy, target, n + 1);

    struct sysfs_dirent *nd = (struct sysfs_dirent *)kmalloc(sizeof(*nd));
    if (!nd)
        return NULL;
    memset(nd, 0, sizeof(*nd));
    nd->kobj = kobj;
    nd->attr = NULL;
    nd->name = name;
    sysfs_init_inode(&nd->inode, FS_SYMLINK, sysfs_alloc_ino(), n, &sys_symlink_ops,
                     (uint32_t)(uintptr_t)kobj, 0777);
    nd->inode.private_data = copy;
    nd->next = sd->children;
    sd->children = nd;
    return &nd->inode;
}
static struct file_operations sys_class_dir_ops;
struct sysfs_named_inode {
    const char *name;
    struct inode *inode;
};

static void sysfs_init_inode(struct inode *inode, uint32_t flags, uint32_t ino,
                             uint32_t size, struct file_operations *f_ops,
                             uint32_t impl, uint32_t mode)
{
    memset(inode, 0, sizeof(*inode));
    inode->flags = flags;
    inode->i_ino = ino;
    inode->i_size = size;
    inode->f_ops = f_ops;
    inode->impl = impl;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = mode;
}

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
        if (!buffer || cap < 2)
            return 0;
        static char tmp[64];
        uint32_t off = 0;
        uint32_t ticks = timer_get_ticks();
        itoa((int)ticks, 10, tmp);
        off = (uint32_t)strlen(tmp);
        if (off + 1 < sizeof(tmp)) {
            tmp[off++] = '\n';
            tmp[off] = 0;
        }
        if (off >= cap)
            off = cap - 1;
        memcpy(buffer, tmp, off);
        if (off < cap)
            buffer[off] = 0;
        return off;
    }

    if (!strcmp(attr->name, "uevent")) {
        if (cap == 0)
            return 0;
        /* Keep old semantics: dump current uevent buffer from offset 0. */
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

static uint32_t sys_read_kobj_attr(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || !node->private_data)
        return 0;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    const struct attribute *attr = (const struct attribute *)node->private_data;
    if (!kobj || !kobj->ktype || !kobj->ktype->sysfs_ops || !kobj->ktype->sysfs_ops->show)
        return 0;

    uint32_t cap = node->i_size ? node->i_size : 256;
    if (cap > 8192)
        cap = 8192;
    char *tmp = (char *)kmalloc(cap + 1);
    if (!tmp)
        return 0;
    tmp[0] = 0;
    uint32_t n = kobj->ktype->sysfs_ops->show(kobj, attr, tmp, cap);
    if (n > cap)
        n = cap;
    tmp[n] = 0;
    if (offset >= n)
        goto out;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
out:
    kfree(tmp);
    return size;
}

static uint32_t sys_write_kobj_attr(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!node || !node->private_data)
        return 0;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    const struct attribute *attr = (const struct attribute *)node->private_data;
    if (!kobj || !kobj->ktype || !kobj->ktype->sysfs_ops || !kobj->ktype->sysfs_ops->store)
        return 0;
    return kobj->ktype->sysfs_ops->store(kobj, attr, offset, size, buffer);
}

static struct file_operations sys_kobj_attr_ops = {
    .read = sys_read_kobj_attr,
    .write = sys_write_kobj_attr,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static int sysfs_kobj_attr_at(struct kobject *kobj, uint32_t index, const struct attribute **out_attr)
{
    if (!kobj || !out_attr)
        return -1;
    uint32_t i = 0;

    if (kobj->ktype && kobj->ktype->default_groups) {
        const struct attribute_group **g = kobj->ktype->default_groups;
        while (g && *g) {
            const struct attribute_group *grp = *g;
            if (grp && grp->attrs) {
                const struct attribute **a = grp->attrs;
                while (a && *a) {
                    uint32_t mode = grp->is_visible ? grp->is_visible(kobj, *a) : (*a)->mode;
                    if (mode) {
                        if (i == index) {
                            *out_attr = *a;
                            return 0;
                        }
                        i++;
                    }
                    a++;
                }
            }
            g++;
        }
    }

    if (kobj->ktype && kobj->ktype->default_attrs) {
        const struct attribute **a = kobj->ktype->default_attrs;
        while (a && *a) {
            uint32_t mode = (*a)->mode;
            if (mode) {
                if (i == index) {
                    *out_attr = *a;
                    return 0;
                }
                i++;
            }
            a++;
        }
    }

    return -1;
}

static uint32_t sysfs_kobj_attr_count(struct kobject *kobj)
{
    uint32_t n = 0;
    const struct attribute *attr;
    while (sysfs_kobj_attr_at(kobj, n, &attr) == 0)
        n++;
    return n;
}

static const struct attribute *sysfs_kobj_find_attr(struct kobject *kobj, const char *name)
{
    if (!kobj || !name)
        return NULL;
    uint32_t i = 0;
    const struct attribute *attr;
    while (sysfs_kobj_attr_at(kobj, i, &attr) == 0) {
        if (attr && attr->name && !strcmp(attr->name, name))
            return attr;
        i++;
    }
    return NULL;
}

static struct dirent *sys_kobj_dir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;

    uint32_t attr_count = sysfs_kobj_attr_count(kobj);
    if (index < attr_count) {
        const struct attribute *attr = NULL;
        if (sysfs_kobj_attr_at(kobj, index, &attr) != 0 || !attr || !attr->name)
            return NULL;
        struct inode *ino = sysfs_get_kobj_attr_inode(kobj, attr);
        if (!ino)
            return NULL;
        strcpy(sys_dirent.name, attr->name);
        sys_dirent.ino = ino->i_ino;
        return &sys_dirent;
    }

    uint32_t next = attr_count;
    if (kobj->parent) {
        if (index == next) {
            struct inode *pino = sysfs_get_kobj_dir_inode(kobj->parent);
            if (!pino)
                return NULL;
            strcpy(sys_dirent.name, "parent");
            sys_dirent.ino = pino->i_ino;
            return &sys_dirent;
        }
        next++;
    }

    struct kobject *child = kobj->children;
    uint32_t i = 0;
    while (child) {
        if (i == index - next) {
            struct inode *cino = sysfs_get_kobj_dir_inode(child);
            if (!cino)
                return NULL;
            strcpy(sys_dirent.name, child->name);
            sys_dirent.ino = cino->i_ino;
            return &sys_dirent;
        }
        i++;
        child = child->next;
    }
    return NULL;
}

static struct inode *sys_kobj_dir_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;

    const struct attribute *attr = sysfs_kobj_find_attr(kobj, name);
    if (attr)
        return sysfs_get_kobj_attr_inode(kobj, attr);

    if (!strcmp(name, "parent") && kobj->parent)
        return sysfs_get_kobj_dir_inode(kobj->parent);

    struct kobject *child = kobj->children;
    while (child) {
        if (!strcmp(child->name, name))
            return sysfs_get_kobj_dir_inode(child);
        child = child->next;
    }
    return NULL;
}

static struct file_operations sys_kobj_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_kobj_dir_readdir,
    .finddir = sys_kobj_dir_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_entry_ops;
static struct file_operations sys_bus_devices_ops;
static struct file_operations sys_bus_drivers_ops;

/* sys_devices_readdir: Implement sys devices readdir. */
static struct dirent *sys_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    uint32_t i = 0;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (cur->parent)
            continue;
        if (i == index) {
            struct inode *ino = sysfs_get_kobj_dir_inode(cur);
            if (!ino)
                return NULL;
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_devices_finddir: Implement sys devices finddir. */
static struct inode *sys_devices_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (cur->parent)
            continue;
        if (!strcmp(cur->name, name)) {
            return sysfs_get_kobj_dir_inode(cur);
        }
    }
    return NULL;
}

/* sys_bus_readdir: Implement sys bus readdir. */
static struct dirent *sys_bus_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_buses_kset();
    if (!kset)
        return NULL;
    uint32_t i = 0;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (i == index) {
            struct inode *ino = sysfs_get_kobj_dir_inode(cur);
            if (!ino)
                return NULL;
            ino->f_ops = &sys_bus_entry_ops;
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_bus_finddir: Implement sys bus finddir. */
static struct inode *sys_bus_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct bus_type *bus = bus_find(name);
    if (!bus)
        return NULL;
    struct inode *ino = sysfs_get_kobj_dir_inode(&bus->kobj);
    if (!ino)
        return NULL;
    ino->f_ops = &sys_bus_entry_ops;
    return ino;
}

static struct dirent *sys_bus_entry_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    if (index == 0) {
        strcpy(sys_dirent.name, "devices");
        struct inode *ino = sysfs_get_kobj_dir_inode(kobj);
        if (!ino)
            return NULL;
        struct inode *dino = sysfs_get_kobj_subdir_inode(kobj, "devices", &sys_bus_devices_ops, 0555);
        if (!dino)
            return NULL;
        sys_dirent.ino = dino->i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "drivers");
        struct inode *dino = sysfs_get_kobj_subdir_inode(kobj, "drivers", &sys_bus_drivers_ops, 0555);
        if (!dino)
            return NULL;
        sys_dirent.ino = dino->i_ino;
        return &sys_dirent;
    }
    return NULL;
}

static struct inode *sys_bus_entry_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    if (!strcmp(name, "devices"))
        return sysfs_get_kobj_subdir_inode(kobj, "devices", &sys_bus_devices_ops, 0555);
    if (!strcmp(name, "drivers"))
        return sysfs_get_kobj_subdir_inode(kobj, "drivers", &sys_bus_drivers_ops, 0555);
    return NULL;
}

static struct dirent *sys_bus_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct bus_type *bus = container_of(kobj, struct bus_type, kobj);
    uint32_t i = 0;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->type && (!strcmp(dev->type, "platform-root") || !strcmp(dev->type, "pci-root")))
            continue;
        if (i == index) {
            strcpy(sys_dirent.name, dev->kobj.name);
            char target[256];
            if (device_get_sysfs_path(dev, target, sizeof(target)) != 0)
                return NULL;
            struct inode *ino = sysfs_get_kobj_link_inode(kobj, dev->kobj.name, target);
            if (!ino)
                return NULL;
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

static struct inode *sys_bus_devices_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct bus_type *bus = container_of(kobj, struct bus_type, kobj);
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->type && (!strcmp(dev->type, "platform-root") || !strcmp(dev->type, "pci-root")))
            continue;
        if (!strcmp(dev->kobj.name, name)) {
            char target[256];
            if (device_get_sysfs_path(dev, target, sizeof(target)) != 0)
                return NULL;
            return sysfs_get_kobj_link_inode(kobj, dev->kobj.name, target);
        }
    }
    return NULL;
}

static struct dirent *sys_bus_drivers_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct bus_type *bus = container_of(kobj, struct bus_type, kobj);
    uint32_t i = 0;
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (i == index) {
            struct inode *ino = sysfs_get_kobj_dir_inode(&drv->kobj);
            if (!ino)
                return NULL;
            strcpy(sys_dirent.name, drv->kobj.name);
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

static struct inode *sys_bus_drivers_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct bus_type *bus = container_of(kobj, struct bus_type, kobj);
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (!strcmp(drv->kobj.name, name)) {
            return sysfs_get_kobj_dir_inode(&drv->kobj);
        }
    }
    return NULL;
}

static struct file_operations sys_bus_entry_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_entry_readdir,
    .finddir = sys_bus_entry_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_devices_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_devices_readdir,
    .finddir = sys_bus_devices_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_drivers_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_drivers_readdir,
    .finddir = sys_bus_drivers_finddir,
    .ioctl = NULL
};

/* sys_class_readdir: Implement sys class readdir. */
static struct dirent *sys_class_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_classes_kset();
    if (!kset)
        return NULL;
    uint32_t i = 0;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (i == index) {
            struct inode *ino = sysfs_get_kobj_dir_inode(cur);
            if (!ino)
                return NULL;
            ino->f_ops = &sys_class_dir_ops;
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_class_finddir: Implement sys class finddir. */
static struct inode *sys_class_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct class *cls = class_find(name);
    if (!cls)
        return NULL;
    struct inode *ino = sysfs_get_kobj_dir_inode(&cls->kobj);
    if (!ino)
        return NULL;
    ino->f_ops = &sys_class_dir_ops;
    return ino;
}

/* sys_class_dir_readdir: Implement sys class dir readdir. */
static struct dirent *sys_class_dir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct class *cls = container_of(kobj, struct class, kobj);
    uint32_t i = 0;
    struct device *cur;
    list_for_each_entry(cur, &cls->devices, class_list) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->kobj.name);
            char target[256];
            if (device_get_sysfs_path(cur, target, sizeof(target)) != 0)
                return NULL;
            struct inode *ino = sysfs_get_kobj_link_inode(kobj, cur->kobj.name, target);
            if (!ino)
                return NULL;
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_class_dir_finddir: Implement sys class dir finddir. */
static struct inode *sys_class_dir_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct class *cls = container_of(kobj, struct class, kobj);
    struct device *cur;
    list_for_each_entry(cur, &cls->devices, class_list) {
        if (!strcmp(cur->kobj.name, name)) {
            char target[256];
            if (device_get_sysfs_path(cur, target, sizeof(target)) != 0)
                return NULL;
            return sysfs_get_kobj_link_inode(kobj, cur->kobj.name, target);
        }
    }
    return NULL;
}

/* sys_readdir: Implement sys readdir. */
static struct dirent *sys_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "kernel");
        struct inode *ino = sysfs_get_kobj_dir_inode(&kernel_kobj);
        if (!ino)
            return NULL;
        sys_dirent.ino = ino->i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "devices");
        sys_dirent.ino = sys_devices.i_ino;
        return &sys_dirent;
    }
    if (index == 2) {
        strcpy(sys_dirent.name, "bus");
        sys_dirent.ino = sys_bus.i_ino;
        return &sys_dirent;
    }
    if (index == 3) {
        strcpy(sys_dirent.name, "class");
        sys_dirent.ino = sys_class.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

/* sys_finddir: Implement sys finddir. */
static struct inode *sys_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "kernel"))
        return sysfs_get_kobj_dir_inode(&kernel_kobj);
    if (!strcmp(name, "devices"))
        return &sys_devices;
    if (!strcmp(name, "bus"))
        return &sys_bus;
    if (!strcmp(name, "class"))
        return &sys_class;
    return NULL;
}

static struct file_operations sys_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_readdir,
    .finddir = sys_finddir,
    .ioctl = NULL
};

static struct file_operations sys_devices_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_devices_readdir,
    .finddir = sys_devices_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_readdir,
    .finddir = sys_bus_finddir,
    .ioctl = NULL
};

static struct file_operations sys_class_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_class_dir_readdir,
    .finddir = sys_class_dir_finddir,
    .ioctl = NULL
};

static struct file_operations sys_class_root_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_class_readdir,
    .finddir = sys_class_finddir,
    .ioctl = NULL
};

/* init_sysfs: Initialize sysfs. */
void init_sysfs(void)
{
    vfs_mount_fs("/sys", "sysfs");
}

/* sysfs_fill_super: Implement sysfs fill super. */
static int sysfs_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    struct inode *sys_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!sys_root)
        return -1;

    memset(sys_root, 0, sizeof(struct inode));
    sys_root->flags = FS_DIRECTORY;
    sys_root->i_ino = 1;
    sys_root->f_ops = &sys_dir_ops;
    sys_root->uid = 0;
    sys_root->gid = 0;
    sys_root->i_mode = 0555;

    kobject_init_with_ktype(&kernel_kobj, "kernel", &kernel_ktype, NULL);

    memset(&sys_devices, 0, sizeof(sys_devices));
    sys_devices.flags = FS_DIRECTORY;
    sys_devices.i_ino = 3;
    sys_devices.f_ops = &sys_devices_dir_ops;
    sys_devices.uid = 0;
    sys_devices.gid = 0;
    sys_devices.i_mode = 0555;

    memset(&sys_bus, 0, sizeof(sys_bus));
    sys_bus.flags = FS_DIRECTORY;
    sys_bus.i_ino = 4;
    sys_bus.f_ops = &sys_bus_dir_ops;
    sys_bus.uid = 0;
    sys_bus.gid = 0;
    sys_bus.i_mode = 0555;

    memset(&sys_class, 0, sizeof(sys_class));
    sys_class.flags = FS_DIRECTORY;
    sys_class.i_ino = 5;
    sys_class.f_ops = &sys_class_root_ops;
    sys_class.uid = 0;
    sys_class.gid = 0;
    sys_class.i_mode = 0555;

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = sys_root;

    return 0;
}

static struct file_system_type sysfs_fs_type = {
    .name = "sysfs",
    .get_sb = vfs_get_sb_single,
    .fill_super = sysfs_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* init_sysfs_fs: Initialize sysfs fs. */
static int init_sysfs_fs(void)
{
    register_filesystem(&sysfs_fs_type);
    printf("sysfs filesystem registered.\n");
    return 0;
}
fs_initcall(init_sysfs_fs);
