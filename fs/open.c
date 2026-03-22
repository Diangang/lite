#include "vfs.h"
#include "task.h"
#include "libc.h"

void open_fs(struct vfs_inode *node, uint8_t read, uint8_t write)
{
    (void)read;
    (void)write;
    if (node->f_ops && node->f_ops->open != NULL)
        node->f_ops->open(node);
}

void close_fs(struct vfs_inode *node)
{
    if (node->f_ops && node->f_ops->close != NULL)
        node->f_ops->close(node);
}

char vfs_boot_cwd[128];

const char *vfs_getcwd(void)
{
    const char *cwd = task_get_cwd();
    if (cwd && *cwd) return cwd;
    return vfs_boot_cwd;
}

int vfs_chdir(const char *path)
{
    if (!path || !*path) return -1;
    char abs[256];
    if (!vfs_build_abs(path, abs, sizeof(abs))) return -1;
    struct vfs_inode *node = vfs_resolve(abs);
    if (!node) return -1;
    if ((node->flags & 0x7) != FS_DIRECTORY) return -1;
    if (!vfs_check_access(node, 0, 0, 1)) return -1;
    if (task_set_cwd(abs) == 0) return 0;
    uint32_t n = (uint32_t)strlen(abs);
    if (n >= sizeof(vfs_boot_cwd)) n = sizeof(vfs_boot_cwd) - 1;
    memcpy(vfs_boot_cwd, abs, n);
    vfs_boot_cwd[n] = 0;
    return 0;
}
