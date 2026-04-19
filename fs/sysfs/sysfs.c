#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/time.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/kobject.h"
#include "linux/ksysfs.h"
#include "base.h"

static struct dirent sys_dirent;
/* sys_root is allocated when sysfs builds a superblock. */
static struct dentry *sysfs_root_dentry;
static struct inode_operations sys_kobj_dir_iops;
static struct inode_operations sys_bus_entry_iops;
static struct inode_operations sys_bus_devices_iops;
static struct inode_operations sys_dir_iops;
static struct inode_operations sys_devices_dir_iops;
static struct inode_operations sys_bus_dir_iops;
static struct inode_operations sys_class_dir_iops;
static struct inode_operations sys_class_root_iops;
static struct inode_operations sys_group_dir_iops;

/* Linux-like internal sysfs node cache. */
struct sysfs_dirent {
    struct inode inode;
    struct kobject *kobj;                 /* Directory owner */
    const struct attribute *attr;         /* File attribute (NULL for directories) */
    char name[64];                        /* Subdir/symlink name ("" for attribute files) */
    struct dentry *dentry;                /* Mounted dcache entry, if materialized */
    int registered;                       /* Dynamic file registered via sysfs_create_file() */
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
static struct file_operations sys_dir_ops;
static struct file_operations sys_devices_dir_ops;
static struct file_operations sys_bus_dir_ops;
static struct file_operations sys_class_dir_ops;
static struct file_operations sys_class_root_ops;
static struct file_operations sys_group_dir_ops;
static struct file_operations sys_dead_ops;
static struct sysfs_dirent *sysfs_get_kobj_sd(struct kobject *kobj);
static struct inode *sysfs_get_kobj_dir_inode(struct kobject *kobj);
static struct sysfs_dirent *sysfs_find_attr_dirent(struct kobject *kobj, const struct attribute *attr);

static void sysfs_dentry_refresh_child(struct dentry *parent, const char *name, struct inode *inode)
{
    if (!parent || !name || !name[0] || !inode)
        return;
    struct dentry *d = d_lookup(parent, name);
    if (!d)
        return;
    d->inode = inode;
    d->d_flags &= ~DENTRY_NEGATIVE;
}

static void sysfs_dentry_detach_child(struct dentry *parent, const char *name)
{
    if (!parent || !name || !name[0])
        return;
    struct dentry *d = d_lookup(parent, name);
    if (!d)
        return;
    vfs_dentry_detach(d);
    d->inode = NULL;
    d->d_flags |= DENTRY_NEGATIVE;
}

static void sysfs_dirent_append_child(struct sysfs_dirent *parent, struct sysfs_dirent *child)
{
    if (!parent || !child)
        return;
    child->next = NULL;
    if (!parent->children) {
        parent->children = child;
        return;
    }
    struct sysfs_dirent *cur = parent->children;
    while (cur->next)
        cur = cur->next;
    cur->next = child;
}

static struct inode_operations *sysfs_dir_iops_for_fops(struct file_operations *f_ops)
{
    if (f_ops == &sys_kobj_dir_ops)
        return &sys_kobj_dir_iops;
    if (f_ops == &sys_bus_entry_ops)
        return &sys_bus_entry_iops;
    if (f_ops == &sys_bus_devices_ops)
        return &sys_bus_devices_iops;
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
    if (f_ops == &sys_group_dir_ops)
        return &sys_group_dir_iops;
    return NULL;
}

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

static struct device *sysfs_kobj_device(struct kobject *kobj)
{
    if (!kobj || kobj->ktype != ktype_device_get())
        return NULL;
    return container_of(kobj, struct device, kobj);
}

static const struct attribute_group **sysfs_device_group_set(struct device *dev, uint32_t index)
{
    if (!dev)
        return NULL;
    switch (index) {
    case 0:
        return dev->groups;
    case 1:
        return (dev->type) ? dev->type->groups : NULL;
    case 2:
        return (dev->class) ? dev->class->dev_groups : NULL;
    case 3:
        return (dev->bus) ? dev->bus->dev_groups : NULL;
    default:
        return NULL;
    }
}

static uint32_t sysfs_group_attr_mode(struct kobject *kobj, const struct attribute_group *grp,
                                      const struct attribute *attr)
{
    if (!grp || !attr)
        return 0;
    return grp->is_visible ? grp->is_visible(kobj, attr) : attr->mode;
}

static int sysfs_group_attr_at(struct kobject *kobj, const struct attribute_group *grp,
                               uint32_t index, const struct attribute **out_attr)
{
    uint32_t i = 0;
    if (!kobj || !grp || !out_attr || !grp->attrs)
        return -1;
    const struct attribute **a = grp->attrs;
    while (a && *a) {
        uint32_t mode = sysfs_group_attr_mode(kobj, grp, *a);
        if (mode) {
            if (i == index) {
                *out_attr = *a;
                return 0;
            }
            i++;
        }
        a++;
    }
    return -1;
}

static const struct attribute *sysfs_group_find_attr(struct kobject *kobj,
                                                     const struct attribute_group *grp,
                                                     const char *name)
{
    uint32_t i = 0;
    const struct attribute *attr;
    if (!kobj || !grp || !name)
        return NULL;
    while (sysfs_group_attr_at(kobj, grp, i, &attr) == 0) {
        if (attr && attr->name && !strcmp(attr->name, name))
            return attr;
        i++;
    }
    return NULL;
}

static uint32_t sysfs_kobj_attr_mode(struct kobject *kobj, const struct attribute *attr)
{
    if (!kobj || !attr)
        return 0;
    struct sysfs_dirent *registered = sysfs_find_attr_dirent(kobj, attr);
    if (registered && registered->registered)
        return attr->mode;
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

    struct device *dev = sysfs_kobj_device(kobj);
    for (uint32_t set = 0; set < 4; set++) {
        const struct attribute_group **dg = sysfs_device_group_set(dev, set);
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
    }
    return attr->mode;
}

static struct kobject *sysfs_parent_kobj(struct kobject *kobj)
{
    if (!kobj)
        return NULL;
    if (kobj->parent)
        return kobj->parent;
    if (kobj->kset && &kobj->kset->kobj != kobj)
        return &kobj->kset->kobj;
    return NULL;
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

static int sysfs_materialize_named_dir(struct sysfs_dirent *parent_sd, struct sysfs_dirent *child_sd)
{
    struct dentry *child;

    if (!parent_sd || !child_sd || !parent_sd->dentry)
        return 0;
    if (child_sd->dentry)
        return 0;
    if (child_sd->attr || !child_sd->name[0] || child_sd->inode.flags != FS_DIRECTORY)
        return 0;

    child = d_lookup(parent_sd->dentry, child_sd->name);
    if (!child)
        child = d_alloc(parent_sd->dentry, child_sd->name);
    if (!child)
        return -1;
    child->inode = &child_sd->inode;
    child->d_flags &= ~DENTRY_NEGATIVE;
    child_sd->dentry = child;
    return 0;
}

static int sysfs_materialize_kobj_subdirs(struct kobject *kobj)
{
    struct sysfs_dirent *sd;
    struct sysfs_dirent *cur;

    if (!kobj)
        return -1;
    sd = sysfs_get_kobj_sd(kobj);
    if (!sd)
        return -1;

    cur = sd->children;
    while (cur) {
        if (sysfs_materialize_named_dir(sd, cur) != 0)
            return -1;
        cur = cur->next;
    }
    return 0;
}

static struct sysfs_dirent *sysfs_find_attr_dirent(struct kobject *kobj, const struct attribute *attr)
{
    struct sysfs_dirent *sd;
    struct sysfs_dirent *cur;

    if (!kobj || !attr)
        return NULL;
    sd = sysfs_get_kobj_sd(kobj);
    if (!sd)
        return NULL;
    cur = sd->children;
    while (cur) {
        if (cur->attr == attr)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

static struct sysfs_dirent *sysfs_find_named_dirent(struct kobject *kobj, const char *name)
{
    struct sysfs_dirent *sd;
    struct sysfs_dirent *cur;

    if (!kobj || !name || !name[0])
        return NULL;
    sd = sysfs_get_kobj_sd(kobj);
    if (!sd)
        return NULL;
    cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name[0] && !strcmp(cur->name, name))
            return cur;
        cur = cur->next;
    }
    return NULL;
}

static struct sysfs_dirent *sysfs_named_subdir_at(struct kobject *kobj, uint32_t index)
{
    struct sysfs_dirent *sd;
    struct sysfs_dirent *cur;
    uint32_t i = 0;

    if (!kobj)
        return NULL;
    sd = sysfs_get_kobj_sd(kobj);
    if (!sd)
        return NULL;
    cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name[0] && cur->inode.flags == FS_DIRECTORY) {
            if (i == index)
                return cur;
            i++;
        }
        cur = cur->next;
    }
    return NULL;
}

static uint32_t sysfs_named_subdir_count(struct kobject *kobj)
{
    struct sysfs_dirent *sd;
    struct sysfs_dirent *cur;
    uint32_t count = 0;

    if (!kobj)
        return 0;
    sd = sysfs_get_kobj_sd(kobj);
    if (!sd)
        return 0;
    cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name[0] && cur->inode.flags == FS_DIRECTORY)
            count++;
        cur = cur->next;
    }
    return count;
}

static struct inode *sysfs_get_kobj_attr_inode(struct kobject *kobj, const struct attribute *attr)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !attr || !attr->name)
        return NULL;

    struct sysfs_dirent *cur = sysfs_find_attr_dirent(kobj, attr);
    if (cur)
        return &cur->inode;

    uint32_t mode = sysfs_kobj_attr_mode(kobj, attr);
    if (!mode)
        return NULL;

    uint32_t size = 256;

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
    sysfs_dirent_append_child(sd, nd);
    return &nd->inode;
}

static int sysfs_create_named_dir(struct kobject *kobj, const char *name, uint32_t mode,
                                  struct file_operations *f_ops, void *private_data)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !name || !name[0] || !f_ops)
        return -1;

    struct sysfs_dirent *cur = sysfs_find_named_dirent(kobj, name);
    if (cur) {
        cur->inode.flags = FS_DIRECTORY;
        cur->inode.i_size = 0;
        cur->inode.i_op = sysfs_dir_iops_for_fops(f_ops);
        cur->inode.f_ops = f_ops;
        cur->inode.impl = (uintptr_t)kobj;
        cur->inode.i_mode = mode;
        cur->inode.private_data = private_data;
        /*
         * If the parent dir is not materialized yet, just update/create the
         * dirent and let sysfs_create_dir()->sysfs_materialize_kobj_subdirs()
         * materialize it later.
         */
        if (sd->dentry && sysfs_materialize_named_dir(sd, cur) != 0)
            return -1;
        return 0;
    }

    struct sysfs_dirent *nd = (struct sysfs_dirent *)kmalloc(sizeof(*nd));
    if (!nd)
        return -1;
    memset(nd, 0, sizeof(*nd));
    nd->kobj = kobj;
    {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(nd->name))
            n = sizeof(nd->name) - 1;
        memcpy(nd->name, name, n);
        nd->name[n] = 0;
    }
    sysfs_init_inode(&nd->inode, FS_DIRECTORY, sysfs_alloc_ino(), 0, f_ops, (uintptr_t)kobj, mode);
    nd->inode.private_data = private_data;
    sysfs_dirent_append_child(sd, nd);
    if (sd->dentry && sysfs_materialize_named_dir(sd, nd) != 0)
        return -1;
    return 0;
}

static void sysfs_ensure_named_group_dirs(struct kobject *kobj)
{
    if (!kobj)
        return;

    if (kobj->ktype && kobj->ktype->default_groups) {
        const struct attribute_group **g = kobj->ktype->default_groups;
        while (g && *g) {
            const struct attribute_group *grp = *g;
            if (grp && grp->name && grp->name[0] && grp->attrs)
                (void)sysfs_create_named_dir(kobj, grp->name, 0555, &sys_group_dir_ops, (void *)grp);
            g++;
        }
    }

    struct device *dev = sysfs_kobj_device(kobj);
    for (uint32_t set = 0; set < 4; set++) {
        const struct attribute_group **dg = sysfs_device_group_set(dev, set);
        while (dg && *dg) {
            const struct attribute_group *grp = *dg;
            if (grp && grp->name && grp->name[0] && grp->attrs)
                (void)sysfs_create_named_dir(kobj, grp->name, 0555, &sys_group_dir_ops, (void *)grp);
            dg++;
        }
    }
}

static struct file_operations *sysfs_subdir_fops(struct kobject *kobj, const char *name)
{
    struct kobject *cur;

    if (kobj && name) {
        list_for_each_entry(cur, &buses_kset_get()->list, entry) {
            if (cur != kobj)
                continue;
            if (!strcmp(name, "devices"))
                return &sys_bus_devices_ops;
            break;
        }
    }

    return &sys_kobj_dir_ops;
}

int sysfs_create_subdir(struct kobject *kobj, const char *name, uint32_t mode)
{
    struct file_operations *f_ops = sysfs_subdir_fops(kobj, name);
    return sysfs_create_named_dir(kobj, name, mode, f_ops, NULL);
}

void sysfs_remove_subdir(struct kobject *kobj, const char *name)
{
    struct sysfs_dirent *cur;

    if (!kobj || !name || !name[0])
        return;
    cur = sysfs_find_named_dirent(kobj, name);
    if (!cur)
        return;
    if (cur->dentry) {
        vfs_dentry_detach(cur->dentry);
        cur->dentry = NULL;
    }
    cur->inode.flags = 0;
    cur->inode.i_size = 0;
    cur->inode.impl = (uintptr_t)0;
    cur->inode.private_data = NULL;
    cur->inode.f_ops = &sys_dead_ops;
    cur->inode.i_op = NULL;
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

static int sysfs_kobject_path(struct kobject *target, char *buf, uint32_t cap)
{
    if (!target || !buf || cap == 0)
        return -1;

    if (target->ktype == ktype_device_get()) {
        struct device *dev = container_of(target, struct device, kobj);
        return device_get_sysfs_path(dev, buf, cap);
    }

    struct kobject *cur;
    list_for_each_entry(cur, &classes_kset_get()->list, entry) {
        if (cur == target) {
            const char *prefix = "/sys/class/";
            uint32_t off = (uint32_t)strlen(prefix);
            uint32_t name_len = (uint32_t)strlen(target->name);
            if (off + name_len + 1 > cap)
                return -1;
            memcpy(buf, prefix, off);
            memcpy(buf + off, target->name, name_len);
            buf[off + name_len] = 0;
            return 0;
        }
    }
    list_for_each_entry(cur, &buses_kset_get()->list, entry) {
        if (cur == target) {
            const char *prefix = "/sys/bus/";
            uint32_t off = (uint32_t)strlen(prefix);
            uint32_t name_len = (uint32_t)strlen(target->name);
            if (off + name_len + 1 > cap)
                return -1;
            memcpy(buf, prefix, off);
            memcpy(buf + off, target->name, name_len);
            buf[off + name_len] = 0;
            return 0;
        }
    }
    if (target->ktype == ktype_driver_get()) {
        struct device_driver *drv = container_of(target, struct device_driver, kobj);
        const char *prefix = "/sys/bus/";
        const char *mid = "/drivers/";
        uint32_t off = (uint32_t)strlen(prefix);
        uint32_t bus_len;
        uint32_t mid_len;
        uint32_t drv_len;
        if (!drv->bus)
            return -1;
        bus_len = (uint32_t)strlen(drv->bus->subsys.kset.kobj.name);
        mid_len = (uint32_t)strlen(mid);
        drv_len = (uint32_t)strlen(drv->kobj.name);
        if (off + bus_len + mid_len + drv_len + 1 > cap)
            return -1;
        memcpy(buf, prefix, off);
        memcpy(buf + off, drv->bus->subsys.kset.kobj.name, bus_len);
        off += bus_len;
        memcpy(buf + off, mid, mid_len);
        off += mid_len;
        memcpy(buf + off, drv->kobj.name, drv_len);
        off += drv_len;
        buf[off] = 0;
        return 0;
    }
    return -1;
}

static int sysfs_relpath(const char *from_dir, const char *to, char *out, uint32_t cap)
{
    if (!from_dir || !to || !out || cap < 2)
        return -1;
    if (from_dir[0] != '/' || to[0] != '/')
        return -1;

    /* Split both absolute paths into components without allocating. */
    const char *from_s[32];
    uint32_t from_n[32];
    const char *to_s[32];
    uint32_t to_n[32];
    uint32_t from_cnt = 0;
    uint32_t to_cnt = 0;

    const char *p = from_dir;
    while (*p == '/')
        p++;
    while (*p && from_cnt < 32) {
        const char *s = p;
        while (*p && *p != '/')
            p++;
        from_s[from_cnt] = s;
        from_n[from_cnt] = (uint32_t)(p - s);
        from_cnt++;
        while (*p == '/')
            p++;
    }

    p = to;
    while (*p == '/')
        p++;
    while (*p && to_cnt < 32) {
        const char *s = p;
        while (*p && *p != '/')
            p++;
        to_s[to_cnt] = s;
        to_n[to_cnt] = (uint32_t)(p - s);
        to_cnt++;
        while (*p == '/')
            p++;
    }

    /* Find common prefix (by path component). */
    uint32_t common = 0;
    while (common < from_cnt && common < to_cnt) {
        if (from_n[common] != to_n[common])
            break;
        int same = 1;
        for (uint32_t j = 0; j < from_n[common]; j++) {
            if (from_s[common][j] != to_s[common][j]) {
                same = 0;
                break;
            }
        }
        if (!same)
            break;
        common++;
    }

    uint32_t off = 0;
    out[0] = 0;

    /* Go up from from_dir to the common prefix. */
    for (uint32_t i = common; i < from_cnt; i++) {
        if (off + 3 >= cap)
            return -1;
        out[off++] = '.';
        out[off++] = '.';
        out[off++] = '/';
        out[off] = 0;
    }

    /* Append remaining to-path components. */
    for (uint32_t i = common; i < to_cnt; i++) {
        uint32_t n = to_n[i];
        if (n == 0)
            continue;
        if (off + n + 1 >= cap)
            return -1;
        memcpy(out + off, to_s[i], n);
        off += n;
        if (i + 1 < to_cnt)
            out[off++] = '/';
        out[off] = 0;
    }

    if (off == 0) {
        out[0] = '.';
        out[1] = 0;
    }

    return 0;
}

static struct inode *sysfs_set_kobj_link_inode(struct kobject *kobj, const char *name, const char *target)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd || !name || !name[0])
        return NULL;

    struct sysfs_dirent *cur = sd->children;
    while (cur) {
        if (!cur->attr && cur->name[0] && !strcmp(cur->name, name)) {
            if (cur->inode.flags == FS_SYMLINK && cur->inode.private_data) {
                kfree(cur->inode.private_data);
                cur->inode.private_data = NULL;
            }
            if (!target || !target[0]) {
                cur->inode.flags = 0;
                cur->inode.i_size = 0;
                cur->inode.f_ops = &sys_dead_ops;
                cur->inode.private_data = NULL;
                return &cur->inode;
            }
            uint32_t n = (uint32_t)strlen(target);
            char *copy = (char *)kmalloc(n + 1);
            if (!copy)
                return NULL;
            memcpy(copy, target, n + 1);
            cur->inode.flags = FS_SYMLINK;
            cur->inode.i_size = n;
            cur->inode.f_ops = &sys_symlink_ops;
            cur->inode.private_data = copy;
            return &cur->inode;
        }
        cur = cur->next;
    }

    if (!target || !target[0])
        return NULL;

    uint32_t n = (uint32_t)strlen(target);
    char *copy = (char *)kmalloc(n + 1);
    if (!copy)
        return NULL;
    memcpy(copy, target, n + 1);

    struct sysfs_dirent *nd = (struct sysfs_dirent *)kmalloc(sizeof(*nd));
    if (!nd) {
        kfree(copy);
        return NULL;
    }
    memset(nd, 0, sizeof(*nd));
    nd->kobj = kobj;
    {
        uint32_t len = (uint32_t)strlen(name);
        if (len >= sizeof(nd->name))
            len = sizeof(nd->name) - 1;
        memcpy(nd->name, name, len);
        nd->name[len] = 0;
    }
    sysfs_init_inode(&nd->inode, FS_SYMLINK, sysfs_alloc_ino(), n, &sys_symlink_ops,
                     (uintptr_t)kobj, 0777);
    nd->inode.private_data = copy;
    sysfs_dirent_append_child(sd, nd);
    return &nd->inode;
}

static struct inode *sysfs_find_kobj_link_inode(struct kobject *kobj, const char *name)
{
    struct sysfs_dirent *cur = sysfs_find_named_dirent(kobj, name);
    if (!cur || cur->inode.f_ops == &sys_dead_ops || cur->inode.flags == 0)
        return NULL;
    return &cur->inode;
}

int sysfs_create_link(struct kobject *kobj, struct kobject *target, const char *name)
{
    char from_abs[256];
    char to_abs[256];
    char rel[256];
    if (!kobj || !target || !name || !name[0])
        return -1;
    if (sysfs_kobject_path(kobj, from_abs, sizeof(from_abs)) != 0)
        return -1;
    if (sysfs_kobject_path(target, to_abs, sizeof(to_abs)) != 0)
        return -1;

    /*
     * Linux mapping: sysfs stores relative symlink targets (e.g. class device
     * "device" link points to "../../devices/...").
     *
     * Lite path walker already resolves relative symlink targets via dentry
     * paths + normalization, so prefer relative targets here.
     */
    const char *target_str = to_abs;
    if (sysfs_relpath(from_abs, to_abs, rel, sizeof(rel)) == 0)
        target_str = rel;

    struct inode *inode = sysfs_set_kobj_link_inode(kobj, name, target_str);
    if (!inode)
        return -1;
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (sd && sd->dentry)
        sysfs_dentry_refresh_child(sd->dentry, name, inode);
    return 0;
}

int sysfs_create_file(struct kobject *kobj, const struct attribute *attr)
{
    struct sysfs_dirent *ad;
    struct inode *inode;

    if (!kobj || !attr || !attr->name)
        return -1;
    inode = sysfs_get_kobj_attr_inode(kobj, attr);
    if (!inode)
        return -1;
    ad = sysfs_find_attr_dirent(kobj, attr);
    if (!ad)
        return -1;
    ad->registered = 1;
    inode->impl = (uintptr_t)kobj;
    inode->private_data = (void *)attr;
    inode->f_ops = &sys_kobj_attr_ops;
    inode->i_mode = attr->mode;
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (sd && sd->dentry)
        sysfs_dentry_refresh_child(sd->dentry, attr->name, inode);
    return 0;
}

int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp)
{
    if (!kobj || !grp || !grp->attrs)
        return -1;
    if (grp->name && grp->name[0])
        return sysfs_create_named_dir(kobj, grp->name, 0555, &sys_group_dir_ops, (void *)grp);
    const struct attribute **attr = grp->attrs;
    while (attr && *attr) {
        uint32_t mode = grp->is_visible ? grp->is_visible(kobj, *attr) : (*attr)->mode;
        if (mode) {
            if (sysfs_create_file(kobj, *attr) != 0)
                return -1;
        }
        attr++;
    }
    return 0;
}

void sysfs_remove_file(struct kobject *kobj, const struct attribute *attr)
{
    struct sysfs_dirent *ad;

    if (!kobj || !attr)
        return;
    ad = sysfs_find_attr_dirent(kobj, attr);
    if (!ad)
        return;
    ad->registered = 0;
    if (ad->dentry) {
        vfs_dentry_detach(ad->dentry);
        ad->dentry = NULL;
    }
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (sd && sd->dentry && attr->name)
        sysfs_dentry_detach_child(sd->dentry, attr->name);
    ad->inode.impl = (uintptr_t)0;
    ad->inode.private_data = NULL;
    ad->inode.f_ops = &sys_dead_ops;
}

void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp)
{
    if (!kobj || !grp || !grp->attrs)
        return;
    if (grp->name && grp->name[0]) {
        sysfs_remove_subdir(kobj, grp->name);
        return;
    }
    const struct attribute **attr = grp->attrs;
    while (attr && *attr) {
        if (!grp->is_visible || grp->is_visible(kobj, *attr))
            sysfs_remove_file(kobj, *attr);
        attr++;
    }
}

void sysfs_remove_link(struct kobject *kobj, const char *name)
{
    (void)sysfs_set_kobj_link_inode(kobj, name, NULL);
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (sd && sd->dentry)
        sysfs_dentry_detach_child(sd->dentry, name);
}

int sysfs_create_dir(struct kobject *kobj)
{
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (!sd)
        return -1;
    sysfs_ensure_named_group_dirs(kobj);
    if (sd->dentry)
        return sysfs_materialize_kobj_subdirs(kobj);

    struct dentry *parent_dentry = NULL;
    struct kobject *parent = sysfs_parent_kobj(kobj);
    if (parent) {
        struct sysfs_dirent *parent_sd = sysfs_get_kobj_sd(parent);
        if (!parent_sd)
            return -1;
        parent_dentry = parent_sd->dentry;
    }

    if (!parent_dentry) {
        if (!sysfs_root_dentry || !kobj->name[0])
            return 0;
        parent_dentry = sysfs_root_dentry;
    }

    struct dentry *child = d_lookup(parent_dentry, kobj->name);
    if (!child)
        child = d_alloc(parent_dentry, kobj->name);
    if (!child)
        return -1;
    child->inode = &sd->inode;
    child->d_flags &= ~DENTRY_NEGATIVE;
    sd->dentry = child;
    return sysfs_materialize_kobj_subdirs(kobj);
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
    if (sd->dentry) {
        vfs_dentry_detach(sd->dentry);
        sd->dentry = NULL;
    }
    sysfs_invalidate_dirent(sd);
}

static struct file_operations sys_class_dir_ops;
struct sysfs_named_inode {
    const char *name;
    struct inode *inode;
};

static uint32_t sys_read_kobj_attr(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || !node->private_data)
        return 0;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    const struct attribute *attr = (const struct attribute *)node->private_data;
    uint32_t (*show)(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap) = NULL;
    struct kobj_attribute *kattr = NULL;
    if (!kobj)
        return 0;
    if (kobj->ktype && kobj->ktype->sysfs_ops && kobj->ktype->sysfs_ops->show) {
        show = kobj->ktype->sysfs_ops->show;
    } else {
        kattr = container_of(attr, struct kobj_attribute, attr);
        if (!kattr || !kattr->show)
            return 0;
    }

    uint32_t cap = node->i_size ? node->i_size : 256;
    if (cap > 8192)
        cap = 8192;
    char *tmp = (char *)kmalloc(cap + 1);
    if (!tmp)
        return 0;
    tmp[0] = 0;
    uint32_t n = show ? show(kobj, attr, tmp, cap) : kattr->show(kobj, kattr, tmp, cap);
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
    if (!(node->i_mode & 0222))
        return 0;
    /*
     * Linux mapping: sysfs attribute store is not a pwrite() interface; writes
     * are treated as full-buffer updates starting at offset 0.
     */
    if (offset != 0)
        return 0;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    const struct attribute *attr = (const struct attribute *)node->private_data;
    uint32_t (*store)(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer) = NULL;
    struct kobj_attribute *kattr = NULL;
    if (!kobj)
        return 0;
    if (kobj->ktype && kobj->ktype->sysfs_ops && kobj->ktype->sysfs_ops->store) {
        store = kobj->ktype->sysfs_ops->store;
    } else {
        kattr = container_of(attr, struct kobj_attribute, attr);
        if (!kattr || !kattr->store)
            return 0;
    }

    if (size > 4096)
        size = 4096;
    char *tmp = (char *)kmalloc(size + 1);
    if (!tmp)
        return 0;
    memcpy(tmp, buffer, size);
    tmp[size] = 0;

    while (size > 0 && (tmp[size - 1] == '\n' || tmp[size - 1] == '\r')) {
        tmp[size - 1] = 0;
        size--;
    }

    uint32_t ret = store ? store(kobj, attr, 0, size, (const uint8_t *)tmp)
                         : kattr->store(kobj, kattr, (const uint8_t *)tmp, size);
    kfree(tmp);
    return ret;
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
                if (grp->name && grp->name[0]) {
                    g++;
                    continue;
                }
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

    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    struct sysfs_dirent *cur = sd ? sd->children : NULL;
    while (cur) {
        if (cur->attr && cur->registered) {
            uint32_t mode = cur->attr->mode;
            if (mode) {
                if (i == index) {
                    *out_attr = cur->attr;
                    return 0;
                }
                i++;
            }
        }
        cur = cur->next;
    }

    struct device *dev = sysfs_kobj_device(kobj);
    for (uint32_t set = 0; set < 4; set++) {
        const struct attribute_group **dg = sysfs_device_group_set(dev, set);
        while (dg && *dg) {
            const struct attribute_group *grp = *dg;
            if (grp && grp->attrs) {
                if (grp->name && grp->name[0]) {
                    dg++;
                    continue;
                }
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

static struct dirent *sys_group_dir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    struct kobject *kobj = node ? (struct kobject *)(uintptr_t)node->impl : NULL;
    const struct attribute_group *grp = node ? (const struct attribute_group *)node->private_data : NULL;
    if (!kobj || !grp)
        return NULL;
    const struct attribute *attr = NULL;
    if (sysfs_group_attr_at(kobj, grp, index, &attr) != 0 || !attr || !attr->name)
        return NULL;
    struct inode *ino = sysfs_get_kobj_attr_inode(kobj, attr);
    if (!ino)
        return NULL;
    strcpy(sys_dirent.name, attr->name);
    sys_dirent.ino = ino->i_ino;
    return &sys_dirent;
}

static struct inode *sys_group_dir_finddir(struct inode *node, const char *name)
{
    struct kobject *kobj = node ? (struct kobject *)(uintptr_t)node->impl : NULL;
    const struct attribute_group *grp = node ? (const struct attribute_group *)node->private_data : NULL;
    if (!kobj || !grp || !name)
        return NULL;
    const struct attribute *attr = sysfs_group_find_attr(kobj, grp, name);
    return attr ? sysfs_get_kobj_attr_inode(kobj, attr) : NULL;
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
    struct inode *pino = sysfs_find_kobj_link_inode(kobj, "parent");
    if (pino) {
        if (index == next) {
            strcpy(sys_dirent.name, "parent");
            sys_dirent.ino = pino->i_ino;
            return &sys_dirent;
        }
        next++;
    }

    struct inode *sino = sysfs_find_kobj_link_inode(kobj, "subsystem");
    if (sino) {
        if (index == next) {
            strcpy(sys_dirent.name, "subsystem");
            sys_dirent.ino = sino->i_ino;
            return &sys_dirent;
        }
        next++;
    }

    struct inode *dino = sysfs_find_kobj_link_inode(kobj, "driver");
    if (dino) {
        if (index == next) {
            strcpy(sys_dirent.name, "driver");
            sys_dirent.ino = dino->i_ino;
            return &sys_dirent;
        }
        next++;
    }

    {
        struct sysfs_dirent *named = sysfs_named_subdir_at(kobj, index - next);
        if (named) {
            strcpy(sys_dirent.name, named->name);
            sys_dirent.ino = named->inode.i_ino;
            return &sys_dirent;
        }
    }
    next += sysfs_named_subdir_count(kobj);

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

    if (!strcmp(name, "parent"))
        return sysfs_find_kobj_link_inode(kobj, "parent");
    if (!strcmp(name, "subsystem"))
        return sysfs_find_kobj_link_inode(kobj, "subsystem");
    if (!strcmp(name, "driver"))
        return sysfs_find_kobj_link_inode(kobj, "driver");

    {
        struct sysfs_dirent *named = sysfs_find_named_dirent(kobj, name);
        if (named && named->inode.flags == FS_DIRECTORY)
            return &named->inode;
    }

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

static struct file_operations sys_group_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_group_dir_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_group_dir_iops = {
    .lookup = sys_group_dir_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations sys_bus_entry_ops;
static struct file_operations sys_bus_devices_ops;

static struct dirent *sys_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    struct kobject *root = node ? (struct kobject *)(uintptr_t)node->impl : NULL;
    if (!root)
        return NULL;
    uint32_t i = 0;
    struct kobject *cur = root->children;
    while (cur) {
        if (i == index) {
            struct inode *ino = sysfs_get_kobj_dir_inode(cur);
            if (!ino)
                return NULL;
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = ino->i_ino;
            return &sys_dirent;
        }
        i++;
        cur = cur->next;
    }
    return NULL;
}

/* sys_devices_finddir: Implement sys devices finddir. */
static struct inode *sys_devices_finddir(struct inode *node, const char *name)
{
    if (!name)
        return NULL;
    struct kobject *root = node ? (struct kobject *)(uintptr_t)node->impl : NULL;
    if (!root)
        return NULL;
    struct kobject *child = root->children;
    while (child) {
        if (!strcmp(child->name, name))
            return sysfs_get_kobj_dir_inode(child);
        child = child->next;
    }
    return NULL;
}

/* sys_bus_readdir: Implement sys bus readdir. */
static struct dirent *sys_bus_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = buses_kset_get();
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
    struct inode *ino = sysfs_get_kobj_dir_inode(&bus->subsys.kset.kobj);
    if (!ino)
        return NULL;
    ino->i_op = &sys_bus_entry_iops;
    ino->f_ops = &sys_bus_entry_ops;
    return ino;
}

static struct dirent *sys_bus_entry_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    struct bus_type *bus;
    if (!node)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    bus = container_of(kobj, struct bus_type, subsys.kset.kobj);
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
    index -= attr_count;
    if (index == 0) {
        strcpy(sys_dirent.name, "devices");
        struct inode *dino = sysfs_get_kobj_dir_inode(bus_devices_kobj(bus));
        if (!dino)
            return NULL;
        sys_dirent.ino = dino->i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "drivers");
        struct inode *dino = sysfs_get_kobj_dir_inode(bus_drivers_kobj(bus));
        if (!dino)
            return NULL;
        sys_dirent.ino = dino->i_ino;
        return &sys_dirent;
    }
    return NULL;
}

static struct inode *sys_bus_entry_finddir(struct inode *node, const char *name)
{
    struct bus_type *bus;
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    bus = container_of(kobj, struct bus_type, subsys.kset.kobj);
    const struct attribute *attr = sysfs_kobj_find_attr(kobj, name);
    if (attr)
        return sysfs_get_kobj_attr_inode(kobj, attr);
    if (!strcmp(name, "devices"))
        return sysfs_get_kobj_dir_inode(bus_devices_kobj(bus));
    if (!strcmp(name, "drivers"))
        return sysfs_get_kobj_dir_inode(bus_drivers_kobj(bus));
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
    struct kobject *owner = kobj->parent ? kobj->parent : kobj;
    struct bus_type *bus = container_of(owner, struct bus_type, subsys.kset.kobj);
    uint32_t i = 0;
    struct klist_iter iter;
    struct klist_node *node_iter;
    struct device *pci_root = pci_root_device();
    klist_iter_init(bus_devices_klist(bus), &iter);
    while ((node_iter = klist_next(&iter)) != NULL) {
        struct device *dev = container_of(node_iter, struct device, knode_bus);
        if (dev == pci_root)
            continue;
        if (i == index) {
            strcpy(sys_dirent.name, dev->kobj.name);
            struct inode *ino = sysfs_find_kobj_link_inode(bus_devices_kobj(bus), dev->kobj.name);
            if (!ino)
                break;
            sys_dirent.ino = ino->i_ino;
            klist_iter_exit(&iter);
            return &sys_dirent;
        }
        i++;
    }
    klist_iter_exit(&iter);
    return NULL;
}

static struct inode *sys_bus_devices_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    struct kobject *kobj = (struct kobject *)(uintptr_t)node->impl;
    if (!kobj)
        return NULL;
    struct kobject *owner = kobj->parent ? kobj->parent : kobj;
    struct bus_type *bus = container_of(owner, struct bus_type, subsys.kset.kobj);
    struct klist_iter iter;
    struct klist_node *node_iter;
    struct device *pci_root = pci_root_device();
    klist_iter_init(bus_devices_klist(bus), &iter);
    while ((node_iter = klist_next(&iter)) != NULL) {
        struct device *dev = container_of(node_iter, struct device, knode_bus);
        if (dev == pci_root)
            continue;
        if (!strcmp(dev->kobj.name, name)) {
            struct inode *ino = sysfs_find_kobj_link_inode(bus_devices_kobj(bus), dev->kobj.name);
            klist_iter_exit(&iter);
            return ino;
        }
    }
    klist_iter_exit(&iter);
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

/* sys_class_readdir: Implement sys class readdir. */
static struct dirent *sys_class_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = classes_kset_get();
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
    struct inode *ino = sysfs_get_kobj_dir_inode(&cls->subsys.kset.kobj);
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
    index -= attr_count;
    struct class *cls = container_of(kobj, struct class, subsys.kset.kobj);
    uint32_t i = 0;
    struct device *cur;
    list_for_each_entry(cur, &cls->devices, class_list) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->kobj.name);
            struct inode *ino = sysfs_find_kobj_link_inode(kobj, cur->kobj.name);
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
    const struct attribute *attr = sysfs_kobj_find_attr(kobj, name);
    if (attr)
        return sysfs_get_kobj_attr_inode(kobj, attr);
    struct class *cls = container_of(kobj, struct class, subsys.kset.kobj);
    struct device *cur;
    list_for_each_entry(cur, &cls->devices, class_list) {
        if (!strcmp(cur->kobj.name, name))
            return sysfs_find_kobj_link_inode(kobj, cur->kobj.name);
    }
    return NULL;
}

static struct file_operations sys_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = generic_readdir,
    .ioctl = NULL
};

static struct inode_operations sys_dir_iops = {
    .lookup = generic_finddir,
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

/* sysfs_mount: Mount sysfs into the initial namespace. */
void sysfs_mount(void)
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

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = sys_root;

    sysfs_root_dentry = sb->s_root;

    struct inode *devices_ino = sysfs_get_kobj_dir_inode(&devices_kset_get()->kobj);
    if (!devices_ino)
        return -1;
    devices_ino->i_op = &sys_devices_dir_iops;
    devices_ino->f_ops = &sys_devices_dir_ops;
    if (sysfs_create_dir(&devices_kset_get()->kobj) != 0)
        return -1;

    struct inode *bus_ino = sysfs_get_kobj_dir_inode(&buses_kset_get()->kobj);
    if (!bus_ino)
        return -1;
    bus_ino->i_op = &sys_bus_dir_iops;
    bus_ino->f_ops = &sys_bus_dir_ops;
    if (sysfs_create_dir(&buses_kset_get()->kobj) != 0)
        return -1;

    struct inode *class_ino = sysfs_get_kobj_dir_inode(&classes_kset_get()->kobj);
    if (!class_ino)
        return -1;
    class_ino->i_op = &sys_class_root_iops;
    class_ino->f_ops = &sys_class_root_ops;
    if (sysfs_create_dir(&classes_kset_get()->kobj) != 0)
        return -1;

    if (kernel_kobj && sysfs_create_dir(kernel_kobj) != 0)
        return -1;

    return 0;
}

static struct file_system_type sysfs_fs_type = {
    .name = "sysfs",
    .get_sb = vfs_get_sb_single,
    .fill_super = sysfs_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* sysfs_init: Register sysfs as a filesystem type. */
int sysfs_init(void)
{
    register_filesystem(&sysfs_fs_type);
    printf("sysfs filesystem registered.\n");
    return 0;
}
