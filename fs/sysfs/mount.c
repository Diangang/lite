// mount.c - Lite sysfs mount/superblock (Linux mapping: fs/sysfs/mount.c)

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

    if (!devices_kset || !bus_kset || !class_kset)
        return -1;

    struct inode *devices_ino = sysfs_get_kobj_dir_inode(&devices_kset->kobj);
    if (!devices_ino)
        return -1;
    devices_ino->i_op = &sys_devices_dir_iops;
    devices_ino->f_ops = &sys_devices_dir_ops;
    if (sysfs_create_dir(&devices_kset->kobj) != 0)
        return -1;

    struct inode *bus_ino = sysfs_get_kobj_dir_inode(&bus_kset->kobj);
    if (!bus_ino)
        return -1;
    bus_ino->i_op = &sys_bus_dir_iops;
    bus_ino->f_ops = &sys_bus_dir_ops;
    if (sysfs_create_dir(&bus_kset->kobj) != 0)
        return -1;

    struct inode *class_ino = sysfs_get_kobj_dir_inode(&class_kset->kobj);
    if (!class_ino)
        return -1;
    class_ino->i_op = &sys_class_root_iops;
    class_ino->f_ops = &sys_class_root_ops;
    if (sysfs_create_dir(&class_kset->kobj) != 0)
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
int sysfs_init(void)
{
    register_filesystem(&sysfs_fs_type);
    printf("sysfs filesystem registered.\n");
    return 0;
}

