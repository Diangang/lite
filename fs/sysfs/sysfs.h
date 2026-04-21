#ifndef FS_SYSFS_SYSFS_H
#define FS_SYSFS_SYSFS_H

#include <stdint.h>
#include "linux/fs.h"

struct kobject;
struct attribute;
struct attribute_group;
struct dentry;
struct file;
struct dirent;

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

/* Shared core helpers provided by fs/sysfs/dir.c */
extern struct dirent sys_dirent;
extern struct dentry *sysfs_root_dentry;
extern struct inode_operations sys_kobj_dir_iops;
extern struct inode_operations sys_bus_entry_iops;
extern struct inode_operations sys_bus_devices_iops;
extern struct inode_operations sys_dir_iops;
extern struct inode_operations sys_devices_dir_iops;
extern struct inode_operations sys_bus_dir_iops;
extern struct inode_operations sys_class_dir_iops;
extern struct inode_operations sys_class_root_iops;
extern struct file_operations sys_kobj_dir_ops;
extern struct file_operations sys_bus_entry_ops;
extern struct file_operations sys_bus_devices_ops;
extern struct file_operations sys_dir_ops;
extern struct file_operations sys_devices_dir_ops;
extern struct file_operations sys_bus_dir_ops;
extern struct file_operations sys_class_dir_ops;
extern struct file_operations sys_class_root_ops;
extern struct file_operations sys_dead_ops;
extern struct file_operations sys_symlink_ops;
uint32_t sysfs_alloc_ino(void);
void sysfs_dirent_append_child(struct sysfs_dirent *parent, struct sysfs_dirent *child);
void sysfs_init_inode(struct inode *inode, uint32_t flags, uint32_t ino, uint32_t size,
                struct file_operations *f_ops, uintptr_t impl, uint32_t mode);
struct sysfs_dirent *sysfs_get_kobj_sd(struct kobject *kobj);
void sysfs_dentry_refresh_child(struct dentry *parent, const char *name, struct inode *inode);
void sysfs_dentry_detach_child(struct dentry *parent, const char *name);
struct inode *sysfs_get_kobj_dir_inode(struct kobject *kobj);
struct sysfs_dirent *sysfs_find_named_dirent(struct kobject *kobj, const char *name);

struct inode *sysfs_set_kobj_link_inode(struct kobject *kobj, const char *name, const char *target);
struct device *sysfs_kobj_device(struct kobject *kobj);
struct kobject *sysfs_parent_kobj(struct kobject *kobj);
int sysfs_create_named_dir(struct kobject *kobj, const char *name, uint32_t mode,
                           struct file_operations *dir_ops, void *private_data);
void sysfs_remove_subdir(struct kobject *kobj, const char *name);
/* Provided by fs/sysfs/file.c */
struct sysfs_dirent *sysfs_find_attr_dirent(struct kobject *kobj, const struct attribute *attr);
struct inode *sysfs_get_kobj_attr_inode(struct kobject *kobj, const struct attribute *attr);
uint32_t sysfs_kobj_attr_mode(struct kobject *kobj, const struct attribute *attr);
int sysfs_create_file(struct kobject *kobj, const struct attribute *attr);
int sysfs_kobj_attr_at(struct kobject *kobj, uint32_t index, const struct attribute **out_attr);
uint32_t sysfs_kobj_attr_count(struct kobject *kobj);
const struct attribute *sysfs_kobj_find_attr(struct kobject *kobj, const char *name);
void sysfs_remove_file(struct kobject *kobj, const struct attribute *attr);


/* Provided by fs/sysfs/group.c */
extern struct file_operations sys_group_dir_ops;
extern struct inode_operations sys_group_dir_iops;
int sysfs_group_attr_at(struct kobject *kobj, const struct attribute_group *grp, uint32_t index, const struct attribute **out_attr);
const struct attribute *sysfs_group_find_attr(struct kobject *kobj, const struct attribute_group *grp, const char *name);
int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp);
void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp);

/* Provided by fs/sysfs/symlink.c */
struct inode *sysfs_find_kobj_link_inode(struct kobject *kobj, const char *name);
int sysfs_create_link(struct kobject *kobj, struct kobject *target, const char *name);
void sysfs_remove_link(struct kobject *kobj, const char *name);

#endif
