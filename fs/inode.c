#include "linux/fs.h"
#include "linux/blkdev.h"
#include "linux/cdev.h"
#include "linux/cred.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/uaccess.h"

/* Global inode allocator for pseudo filesystems */
static uint32_t last_ino = 100; // Start at 100 to avoid conflicts with special statically assigned ones like root(1) or proc entries

/* get_next_ino: Get next ino. */
uint32_t get_next_ino(void)
{
    // In a real SMP kernel, this would be an atomic operation or per-CPU counter
    return ++last_ino;
}

void init_special_inode(struct inode *inode, uint32_t type, dev_t devt, struct file_operations *f_ops)
{
    if (!inode)
        return;
    inode->flags = type;
    inode->i_ino = get_next_ino();
    inode->impl = (uintptr_t)devt;
    inode->f_ops = f_ops;
}

struct inode *alloc_special_inode(uint32_t type, dev_t devt, struct file_operations *f_ops,
                                  uint32_t mode, uint32_t uid, uint32_t gid)
{
    struct inode *inode = (struct inode *)kmalloc(sizeof(*inode));

    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    init_special_inode(inode, type, devt, f_ops);
    inode->i_mode = (mode & 0777);
    inode->uid = uid;
    inode->gid = gid;
    return inode;
}

struct inode *create_special_inode(uint32_t type, dev_t devt, void *private_data,
                                   uint32_t mode, uint32_t uid, uint32_t gid)
{
    if (type == FS_CHARDEVICE)
        return chrdev_inode_create(devt, mode, uid, gid);
    if (type == FS_BLOCKDEVICE) {
        struct block_device *bdev = (struct block_device *)private_data;
        struct inode *inode = blockdev_inode_create(bdev);
        if (!inode)
            return NULL;
        inode->i_mode = (mode & 0777);
        inode->uid = uid;
        inode->gid = gid;
        return inode;
    }
    return NULL;
}

void destroy_special_inode(struct inode *inode)
{
    if (!inode)
        return;
    if ((inode->flags & 0x7) == FS_CHARDEVICE) {
        chrdev_inode_destroy(inode);
        return;
    }
    if ((inode->flags & 0x7) == FS_BLOCKDEVICE) {
        struct block_device *bdev = (struct block_device *)inode->private_data;
        blockdev_inode_destroy(bdev);
        return;
    }
}

int special_inode_matches(struct inode *inode, uint32_t type, dev_t devt)
{
    if (!inode)
        return 0;
    if ((inode->flags & 0x7) != type)
        return 0;
    if (type == FS_BLOCKDEVICE) {
        struct block_device *bdev = (struct block_device *)inode->private_data;
        return bdev && bdev->devt == devt;
    }
    if (type == FS_CHARDEVICE)
        return (dev_t)inode->impl == devt;
    return 0;
}

/* vfs_check_access: Implement vfs check access. */
int vfs_check_access(struct inode *node, int want_read, int want_write, int want_exec)
{
    if (!node)
        return 0;
    uint32_t uid = current_uid();
    uint32_t gid = current_gid();
    if (uid == 0)
        return 1;
    uint32_t mode = node->i_mode & 0777;
    uint32_t bits;
    if (uid == node->uid)
        bits = (mode >> 6) & 0x7; else if (gid == node->gid)
        bits = (mode >> 3) & 0x7; else {
        bits = mode & 0x7;
    }
    if (want_read && !(bits & 0x4))
        return 0;
    if (want_write && !(bits & 0x2))
        return 0;
    if (want_exec && !(bits & 0x1))
        return 0;
    return 1;
}

/* vfs_chmod: Implement vfs chmod. */
int vfs_chmod(const char *path, uint32_t mode)
{
    struct inode *node = vfs_resolve(path);
    if (!node)
        return -1;
    uint32_t uid = current_uid();
    if (uid != 0 && uid != node->uid)
        return -1;
    node->i_mode = mode & 0777;
    return 0;
}

/* sys_chmod: Implement sys chmod. */
int sys_chmod(const char *pathname, uint32_t mode, int from_user)
{
    char tmp[128];
    if (from_user) {
        if (strncpy_from_user(tmp, sizeof(tmp), pathname) != 0)
            return -1;
    } else {
        if (!pathname)
            return -1;
        strcpy(tmp, pathname);
    }
    return vfs_chmod(tmp, mode) == 0 ? 0 : -1;
}
