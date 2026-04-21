// group.c - Lite sysfs attribute groups (Linux mapping: fs/sysfs/group.c)

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

static struct dirent *sys_group_dir_readdir(struct file *file, uint32_t index);
static struct inode *sys_group_dir_finddir(struct inode *node, const char *name);

struct file_operations sys_group_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_group_dir_readdir,
    .ioctl = NULL
};
struct inode_operations sys_group_dir_iops = {
    .lookup = sys_group_dir_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};
static uint32_t sysfs_group_attr_mode(struct kobject *kobj, const struct attribute_group *grp,
                                      const struct attribute *attr)
{
    if (!grp || !attr)
        return 0;
    return grp->is_visible ? grp->is_visible(kobj, attr) : attr->mode;
}
int sysfs_group_attr_at(struct kobject *kobj, const struct attribute_group *grp,
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
const struct attribute *sysfs_group_find_attr(struct kobject *kobj,
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
