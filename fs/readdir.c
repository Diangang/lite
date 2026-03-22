#include "vfs.h"
#include "libc.h"

struct dirent *readdir_fs(struct vfs_inode *node, uint32_t index)
{
    if ((node->flags & 0x7) == FS_DIRECTORY && node->f_ops && node->f_ops->readdir != NULL)
        return node->f_ops->readdir(node, index);
    return NULL;
}

struct vfs_inode *finddir_fs(struct vfs_inode *node, const char *name)
{
    if (!node || !name) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY || !node->f_ops || node->f_ops->finddir == NULL) return NULL;

    while (*name == '/') name++;
    if (*name == 0) return node;

    const char *slash = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '/') {
            slash = p;
            break;
        }
    }

    if (!slash) {
        struct vfs_inode *res = node->f_ops->finddir(node, name);
        return res;
    }

    char part[128];
    uint32_t n = (uint32_t)(slash - name);
    if (n == 0 || n >= sizeof(part)) return NULL;
    memcpy(part, name, n);
    part[n] = 0;

    struct vfs_inode *child = node->f_ops->finddir(node, part);
    if (!child) return NULL;

    while (*slash == '/') slash++;
    if (*slash == 0) return child;
    return finddir_fs(child, slash);
}

static struct dirent vfs_root_dirent;

static int vfs_is_root_child_mount(const char *mount_path)
{
    if (!mount_path) return 0;
    if (mount_path[0] != '/') return 0;
    if (mount_path[1] == 0) return 0;
    for (const char *p = mount_path + 1; *p; p++) {
        if (*p == '/') return 0;
    }
    return 1;
}
static struct dirent *vfs_root_readdir(struct vfs_inode *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(vfs_root_dirent.name, ".");
        vfs_root_dirent.ino = node->inode;
        return &vfs_root_dirent;
    }
    if (index == 1) {
        strcpy(vfs_root_dirent.name, "..");
        vfs_root_dirent.ino = node->inode;
        return &vfs_root_dirent;
    }

    index -= 2;

    uint32_t n_mounts = 0;
    for (uint32_t i = 0; i < vfs_mount_count; i++) {
        if (vfs_is_root_child_mount(vfs_mounts[i].path)) n_mounts++;
    }
    if (index < n_mounts) {
        uint32_t k = 0;
        for (uint32_t i = 0; i < vfs_mount_count; i++) {
            if (!vfs_is_root_child_mount(vfs_mounts[i].path)) continue;
            if (k == index) {
                strcpy(vfs_root_dirent.name, vfs_mounts[i].path + 1);
                vfs_root_dirent.ino = 0xEEEE1000 + i;
                return &vfs_root_dirent;
            }
            k++;
        }
        return NULL;
    }

    if (!vfs_base_root) return NULL;

    // index represents the virtual index in the merged directory
    // We already yielded 2 virtual entries (. and ..) and n_mounts virtual entries
    // For ramfs, index 0 is . and 1 is ..
    // We want virtual index `n_mounts` to map to ramfs index 2.
    // So ramfs_idx = index - n_mounts + 2.
    // BUT WAIT: what if we just ask ramfs for exactly what it has, and we just skip?
    // If the user's f->pos advances one by one, we must return exactly ONE valid entry per index.

    // When index == n_mounts, we want to fetch the first REAL child of ramfs.
    // In ramfs, index 0 is '.', 1 is '..', 2 is first child.
    // BUT we must dynamically skip children that have the same name as mount points!
    // Since we can't reliably map the virtual index to ramfs index directly if we skip items,
    // we must iterate.
    uint32_t ramfs_idx = 2; // start at first real child
    uint32_t target = index - n_mounts;
    uint32_t valid_found = 0;

    while (1) {
        struct dirent *d = readdir_fs(vfs_base_root, ramfs_idx);
        if (!d) return NULL; // End of ramfs directory

        // Check if shadowed
        int shadowed = 0;
        for (uint32_t i = 0; i < vfs_mount_count; i++) {
            if (!vfs_is_root_child_mount(vfs_mounts[i].path)) continue;
            if (!strcmp(d->name, vfs_mounts[i].path + 1)) {
                shadowed = 1;
                break;
            }
        }

        if (!shadowed) {
            if (valid_found == target) {
                return d;
            }
            valid_found++;
        }
        ramfs_idx++;
    }
}

static struct vfs_inode *vfs_root_finddir(struct vfs_inode *node, const char *name)
{
    (void)node;
    if (!name) return NULL;

    // Ignore leading slash
    while (*name == '/') name++;
    if (*name == 0) return &vfs_root_node;

    for (uint32_t i = 0; i < vfs_mount_count; i++) {
        if (!vfs_is_root_child_mount(vfs_mounts[i].path)) continue;
        if (!strcmp(name, vfs_mounts[i].path + 1)) {
            if (!vfs_mounts[i].sb) return NULL;
            return vfs_mounts[i].sb->root;
        }
    }
    if (!vfs_base_root) return NULL;
    struct vfs_inode *node_found = finddir_fs(vfs_base_root, name);
    if (!node_found) return NULL;
    return node_found;
}

struct vfs_file_operations vfs_root_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = vfs_root_readdir,
    .finddir = vfs_root_finddir,
    .ioctl = NULL
};
