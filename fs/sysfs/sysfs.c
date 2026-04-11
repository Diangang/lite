#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/timer.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/kobject.h"
#include "linux/ksysfs.h"
#include "base.h"

static struct dirent sys_dirent;
// Note: sys_root is dynamically allocated in init_sysfs now
static struct inode sys_devices;
static struct inode sys_bus;
static struct inode sys_class;
static struct inode_operations sys_kobj_dir_iops;
static struct inode_operations sys_bus_entry_iops;
static struct inode_operations sys_bus_devices_iops;
static struct inode_operations sys_bus_drivers_iops;
static struct inode_operations sys_dir_iops;
static struct inode_operations sys_devices_dir_iops;
static struct inode_operations sys_bus_dir_iops;
static struct inode_operations sys_class_dir_iops;
static struct inode_operations sys_class_root_iops;

static void sysfs_init_inode(struct inode *inode, uint32_t flags, uint32_t ino,
                             uint32_t size, struct file_operations *f_ops,
                             uintptr_t impl, uint32_t mode);
static struct inode_operations *sysfs_dir_iops_for_fops(struct file_operations *f_ops);

/* Linux-like internal sysfs node cache. */
struct sysfs_dirent {
    struct inode inode;
    struct kobject *kobj;                 /* Directory owner */
    const struct attribute *attr;         /* File attribute (NULL for directories) */
    char name[64];                        /* Subdir/symlink name ("" for attribute files) */
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
static struct file_operations sys_bus_entry_ops;
static struct file_operations sys_bus_devices_ops;
static struct file_operations sys_bus_drivers_ops;
static struct file_operations sys_dir_ops;
static struct file_operations sys_devices_dir_ops;
static struct file_operations sys_bus_dir_ops;
static struct file_operations sys_class_dir_ops;
static struct file_operations sys_class_root_ops;

static const struct attribute_group **sysfs_device_groups(struct kobject *kobj)
{
    if (!kobj || kobj->ktype != device_model_device_ktype())
        return NULL;
    struct device *dev = container_of(kobj, struct device, kobj);
    return dev ? dev->groups : NULL;
}

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

    const struct attribute_group **dg = sysfs_device_groups(kobj);
    while (dg && *dg) {
        const struct attribute_group *grp = *dg;
        if (grp && grp->attrs) {
            const struct attribute **a = grp->attrs;
            while (a && *a) {
                if (*a == attr) {
                    if (grp->is_visible)
                        return grp->is_visible(kobj, attr);
                    return attr->mode;
                }
                a++;
            }
        }
        dg++;
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
                     (uintptr_t)kobj, 0555);
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
    nd->name[0] = 0;
    sysfs_init_inode(&nd->inode, FS_FILE, sysfs_alloc_ino(), size, &sys_kobj_attr_ops,
                     (uintptr_t)kobj, mode);
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
        if (!cur->attr && cur->name[0] && !strcmp(cur->name, name))
            return &cur->inode;
        cur = cur->next;
    }

    struct sysfs_dirent *nd = (struct sysfs_dirent *)kmalloc(sizeof(*nd));
    if (!nd)
        return NULL;
    memset(nd, 0, sizeof(*nd));
    nd->kobj = kobj;
    nd->attr = NULL;
    {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(nd->name))
            n = sizeof(nd->name) - 1;
        memcpy(nd->name, name, n);
        nd->name[n] = 0;
    }
    sysfs_init_inode(&nd->inode, FS_DIRECTORY, sysfs_alloc_ino(), 0, f_ops, (uintptr_t)kobj, mode);
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
    .ioctl = NULL
};

static uint32_t sys_dead_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0;
}

static uint32_t sys_dead_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0;
}

static struct dirent *sys_dead_readdir(struct file *file, uint32_t index)
{
    (void)file;
    (void)index;
    return NULL;
}

static struct file_operations sys_dead_ops = {
    .read = sys_dead_read,
    .write = sys_dead_write,
    .open = NULL,
    .close = NULL,
    .readdir = sys_dead_readdir,
    .ioctl = NULL
};

static void sysfs_invalidate_dirent(struct sysfs_dirent *sd)
{
    if (!sd)
        return;

    /* Make this inode safe to touch even after its owner kobject is freed. */
    sd->inode.impl = (uintptr_t)0;
    sd->inode.private_data = NULL;
    sd->inode.f_ops = &sys_dead_ops;

    struct sysfs_dirent *cur = sd->children;
    while (cur) {
        cur->inode.impl = (uintptr_t)0;
        if (cur->inode.flags == FS_SYMLINK && cur->inode.private_data) {
            kfree(cur->inode.private_data);
            cur->inode.private_data = NULL;
        }
        cur->inode.f_ops = &sys_dead_ops;
        cur = cur->next;
    }
}

/*
 * Linux alignment note:
 * Linux sysfs/kernfs removes nodes when the backing kobject goes away, so it is
 * impossible to reach stale sysfs inodes. Lite currently keeps dentries forever,
 * so we "invalidate" the cached sysfs inodes on teardown to avoid UAF.
 */
void sysfs_remove_dir(struct kobject *kobj)
{
    if (!kobj || !kobj->sd)
        return;
    struct sysfs_dirent *sd = (struct sysfs_dirent *)kobj->sd;
    kobj->sd = NULL;
    sysfs_invalidate_dirent(sd);
}

static struct inode *sysfs_get_kobj_link_inode(struct kobject *kobj, const char *name, const char *target)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !name || !name[0] || !target || !target[0])
        return NULL;

    struct sysfs_dirent *cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name[0] && cur->inode.flags == FS_SYMLINK && !strcmp(cur->name, name))
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
    {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(nd->name))
            n = sizeof(nd->name) - 1;
        memcpy(nd->name, name, n);
        nd->name[n] = 0;
    }
    sysfs_init_inode(&nd->inode, FS_SYMLINK, sysfs_alloc_ino(), n, &sys_symlink_ops,
                     (uintptr_t)kobj, 0777);
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
                             uintptr_t impl, uint32_t mode)
{
    memset(inode, 0, sizeof(*inode));
    inode->flags = flags;
    inode->i_ino = ino;
    inode->i_size = size;
    inode->i_op = (flags == FS_DIRECTORY) ? sysfs_dir_iops_for_fops(f_ops) : NULL;
    inode->f_ops = f_ops;
    inode->impl = impl;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = mode;
}

static struct inode_operations *sysfs_dir_iops_for_fops(struct file_operations *f_ops)
{
    if (f_ops == &sys_kobj_dir_ops)
        return &sys_kobj_dir_iops;
    if (f_ops == &sys_bus_entry_ops)
        return &sys_bus_entry_iops;
    if (f_ops == &sys_bus_devices_ops)
        return &sys_bus_devices_iops;
    if (f_ops == &sys_bus_drivers_ops)
        return &sys_bus_drivers_iops;
    if (f_ops == &sys_dir_ops)
        return &sys_dir_iops;
    if (f_ops == &sys_devices_dir_ops)
        return &sys_devices_dir_iops;
    if (f_ops == &sys_bus_dir_ops)
        return &sys_bus_dir_iops;
    if (f_ops == &sys_class_dir_ops)
        return &sys_class_dir_iops;
    if (f_ops == &sys_class_root_ops)
        return &sys_class_root_iops;
    return NULL;
}

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
    if (offset >= n) {
        size = 0;
    } else {
        uint32_t remain = n - offset;
        if (size > remain)
            size = remain;
        memcpy(buffer, tmp + offset, size);
    }
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

    const struct attribute_group **dg = sysfs_device_groups(kobj);
    while (dg && *dg) {
        const struct attribute_group *grp = *dg;
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
        dg++;
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
    .ioctl = NULL
};

static struct inode_operations sys_kobj_dir_iops = {
    .lookup = sys_kobj_dir_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
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
            ino->i_op = &sys_bus_entry_iops;
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
    ino->i_op = &sys_bus_entry_iops;
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
    struct device *platform_root = device_model_platform_root();
    struct device *pci_root = device_model_pci_root();
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev == platform_root || dev == pci_root)
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
    struct device *platform_root = device_model_platform_root();
    struct device *pci_root = device_model_pci_root();
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev == platform_root || dev == pci_root)
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
    .ioctl = NULL
};

static struct inode_operations sys_bus_entry_iops = {
    .lookup = sys_bus_entry_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_bus_devices_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_devices_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_bus_devices_iops = {
    .lookup = sys_bus_devices_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_bus_drivers_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_drivers_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_bus_drivers_iops = {
    .lookup = sys_bus_drivers_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
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
            ino->i_op = &sys_class_dir_iops;
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
    ino->i_op = &sys_class_dir_iops;
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
    .ioctl = NULL
};

static struct inode_operations sys_dir_iops = {
    .lookup = sys_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_devices_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_devices_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_devices_dir_iops = {
    .lookup = sys_devices_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_bus_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_bus_dir_iops = {
    .lookup = sys_bus_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_class_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_class_dir_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_class_dir_iops = {
    .lookup = sys_class_dir_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_class_root_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_class_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_class_root_iops = {
    .lookup = sys_class_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
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
    sys_root->i_op = &sys_dir_iops;
    sys_root->f_ops = &sys_dir_ops;
    sys_root->uid = 0;
    sys_root->gid = 0;
    sys_root->i_mode = 0555;

    memset(&sys_devices, 0, sizeof(sys_devices));
    sys_devices.flags = FS_DIRECTORY;
    sys_devices.i_ino = 3;
    sys_devices.i_op = &sys_devices_dir_iops;
    sys_devices.f_ops = &sys_devices_dir_ops;
    sys_devices.uid = 0;
    sys_devices.gid = 0;
    sys_devices.i_mode = 0555;

    memset(&sys_bus, 0, sizeof(sys_bus));
    sys_bus.flags = FS_DIRECTORY;
    sys_bus.i_ino = 4;
    sys_bus.i_op = &sys_bus_dir_iops;
    sys_bus.f_ops = &sys_bus_dir_ops;
    sys_bus.uid = 0;
    sys_bus.gid = 0;
    sys_bus.i_mode = 0555;

    memset(&sys_class, 0, sizeof(sys_class));
    sys_class.flags = FS_DIRECTORY;
    sys_class.i_ino = 5;
    sys_class.i_op = &sys_class_root_iops;
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
