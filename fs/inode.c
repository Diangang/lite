#include "linux/fs.h"
#include "linux/cred.h"
#include "linux/libc.h"
#include "linux/uaccess.h"

/* Global inode allocator for pseudo filesystems */
static uint32_t last_ino = 100; // Start at 100 to avoid conflicts with special statically assigned ones like root(1) or proc entries

uint32_t get_next_ino(void)
{
    // In a real SMP kernel, this would be an atomic operation or per-CPU counter
    return ++last_ino;
}

int vfs_check_access(struct inode *node, int want_read, int want_write, int want_exec)
{
    if (!node)
        return 0;
    uint32_t uid = task_get_uid();
    uint32_t gid = task_get_gid();
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

int vfs_chmod(const char *path, uint32_t mode)
{
    struct inode *node = vfs_resolve(path);
    if (!node)
        return -1;
    uint32_t uid = task_get_uid();
    if (uid != 0 && uid != node->uid)
        return -1;
    node->i_mode = mode & 0777;
    return 0;
}

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
