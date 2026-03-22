#include "fs.h"
#include "task.h"
#include "libc.h"

int vfs_check_access(struct vfs_inode *node, int want_read, int want_write, int want_exec)
{
    if (!node) return 0;
    uint32_t uid = task_get_uid();
    uint32_t gid = task_get_gid();
    if (uid == 0) return 1;
    uint32_t mode = node->mask & 0777;
    uint32_t bits;
    if (uid == node->uid) {
        bits = (mode >> 6) & 0x7;
    } else if (gid == node->gid) {
        bits = (mode >> 3) & 0x7;
    } else {
        bits = mode & 0x7;
    }
    if (want_read && !(bits & 0x4)) return 0;
    if (want_write && !(bits & 0x2)) return 0;
    if (want_exec && !(bits & 0x1)) return 0;
    return 1;
}

int vfs_chmod(const char *path, uint32_t mode)
{
    struct vfs_inode *node = vfs_resolve(path);
    if (!node) return -1;
    uint32_t uid = task_get_uid();
    if (uid != 0 && uid != node->uid) return -1;
    node->mask = mode & 0777;
    return 0;
}
