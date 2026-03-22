#include "vfs.h"
#include "kheap.h"
#include "libc.h"
#include "ramfs.h"
#include "task.h"
#include "initrd.h"
#include "procfs.h"
#include "devfs.h"
#include "sysfs.h"
#include "device_model.h"
#include "console.h"
#include "multiboot.h"

struct vfs_mount vfs_mounts[8];
uint32_t vfs_mount_count = 0;
struct vfs_inode vfs_root_node;
struct vfs_inode *vfs_base_root = NULL;
static struct vfs_dentry *vfs_root_dentry = NULL;

int vfs_mount_root(const char *path, struct vfs_inode *root_node)
{
    if (!path || !root_node)
        panic("mount path null.");

    if (vfs_mount_count >= (sizeof(vfs_mounts) / sizeof(vfs_mounts[0])))
        panic("mount path exceeded.");

    struct vfs_mount *m = &vfs_mounts[vfs_mount_count++];
    m->path = path;
    m->sb = (struct vfs_super_block*)kmalloc(sizeof(struct vfs_super_block));
    if (!m->sb)
        panic("mount path failed for alloc.");

    m->sb->name = path;
    m->sb->refcount = 1;
    if (path[0] == '/' && path[1] == 0) {
        vfs_base_root = root_node;
        vfs_root_node.f_ops = &vfs_root_ops;
        vfs_root_node.inode = root_node->inode; // Sync inode
        m->sb->root = &vfs_root_node;
    } else {
        m->sb->root = root_node;
    }
    m->sb->fs_private = NULL;
    printf("mount %s success.\n", path);
    return 0;
}

void init_vfs(void)
{
    memset(vfs_mounts, 0, sizeof(vfs_mounts));
    vfs_mount_count = 0;
    memset(&vfs_root_node, 0, sizeof(vfs_root_node));
    vfs_root_node.flags = FS_DIRECTORY;
    vfs_root_node.inode = 0xEEEE0001;
    vfs_root_node.f_ops = &vfs_root_ops; // Initialize early!
    vfs_root_node.uid = 0;
    vfs_root_node.gid = 0;
    vfs_root_node.mask = 0555;
    vfs_root_dentry = vfs_dentry_get(&vfs_root_node, "/");
    vfs_base_root = NULL;
    // Set cwd to root initially without resolving
    strcpy(vfs_boot_cwd, "/");
}
