// file.c - Lite sysfs attribute files (Linux mapping: fs/sysfs/file.c)

#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/init.h"
#include "linux/time.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/kobject.h"
#include "linux/kobject.h"
#include "base.h"
#include "sysfs/sysfs.h"

static uint32_t sys_read_kobj_attr(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t sys_write_kobj_attr(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer);

static struct file_operations sys_kobj_attr_ops = {
    .read = sys_read_kobj_attr,
    .write = sys_write_kobj_attr,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};
uint32_t sysfs_kobj_attr_mode(struct kobject *kobj, const struct attribute *attr)
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
struct sysfs_dirent *sysfs_find_attr_dirent(struct kobject *kobj, const struct attribute *attr)
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
struct inode *sysfs_get_kobj_attr_inode(struct kobject *kobj, const struct attribute *attr)
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
int sysfs_kobj_attr_at(struct kobject *kobj, uint32_t index, const struct attribute **out_attr)
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
uint32_t sysfs_kobj_attr_count(struct kobject *kobj)
{
    uint32_t n = 0;
    const struct attribute *attr;
    while (sysfs_kobj_attr_at(kobj, n, &attr) == 0)
        n++;
    return n;
}
const struct attribute *sysfs_kobj_find_attr(struct kobject *kobj, const char *name)
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
