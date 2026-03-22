#include "fs.h"
#include "libc.h"
#include "task.h"
#include "ramfs.h"

// Keep vfs_build_abs to build absolute path strings for simplicity
// Alternatively, we could do path_walk from task cwd dentry without strings, but this is simpler
// because task struct currently stores cwd as string.
static int vfs_append_component(char *out, uint32_t *len, uint32_t cap, const char *start, uint32_t n)
{
    if (!out || !len || !start) return 0;
    if (*len == 0) {
        if (cap < 2) return 0;
        out[0] = '/';
        out[1] = 0;
        *len = 1;
    }
    if (n == 0) return 1;
    if (*len + 1 >= cap) return 0;
    if (out[*len - 1] != '/') {
        out[*len] = '/';
        (*len)++;
        out[*len] = 0;
    }
    if (*len + n >= cap) return 0;
    memcpy(out + *len, start, n);
    *len += n;
    out[*len] = 0;
    return 1;
}

static void vfs_pop_component(char *out, uint32_t *len)
{
    if (!out || !len) return;
    if (*len <= 1) {
        out[0] = '/';
        out[1] = 0;
        *len = 1;
        return;
    }
    uint32_t i = *len - 1;
    while (i > 0 && out[i] != '/') i--;
    if (i == 0) {
        out[1] = 0;
        *len = 1;
    } else {
        out[i] = 0;
        *len = i;
    }
}

int vfs_normalize_path(const char *path, char *out, uint32_t cap)
{
    if (!path || !out || cap < 2) return 0;
    uint32_t len = 0;
    out[0] = 0;
    const char *p = path;
    while (*p == '/') p++;
    if (!vfs_append_component(out, &len, cap, "", 0)) return 0;

    while (*p) {
        const char *s = p;
        while (*p && *p != '/') p++;
        uint32_t n = (uint32_t)(p - s);
        if (n == 1 && s[0] == '.') {
            // skip
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            vfs_pop_component(out, &len);
        } else if (n > 0) {
            if (!vfs_append_component(out, &len, cap, s, n)) return 0;
        }
        while (*p == '/') p++;
    }
    return 1;
}

// Removed vfs_build_abs entirely, no longer needed as path_walk now walks relative to dentry

struct dentry *path_walk(const char *path)
{
    if (!path || !*path) return NULL;

    struct dentry *curr;
    if (path[0] == '/') {
        curr = task_get_root_dentry();
        if (!curr) curr = vfs_root_dentry; // Fallback for very early boot
    } else {
        curr = task_get_cwd_dentry();
        if (!curr) curr = vfs_root_dentry;
    }
    
    if (!curr) return NULL;

    const char *p = path;
    while (*p == '/') p++;

    while (*p) {
        const char *s = p;
        while (*p && *p != '/') p++;
        uint32_t len = (uint32_t)(p - s);

        char part[128];
        if (len >= sizeof(part)) return NULL;
        memcpy(part, s, len);
        part[len] = 0;

        if (strcmp(part, ".") == 0) {
            // curr stays the same
        } else if (strcmp(part, "..") == 0) {
            if (curr->parent) curr = curr->parent;
        } else {
            struct dentry *next = d_lookup(curr, part);
            if (!next) {
                // Not in cache, try underlying FS
                if (curr->inode && curr->inode->f_ops && curr->inode->f_ops->finddir) {
                    struct inode *child_inode = curr->inode->f_ops->finddir(curr->inode, part);
                    if (child_inode) {
                        next = d_alloc(curr, part);
                        next->inode = child_inode;
                    }
                }
            }
            if (!next) return NULL; // Not found
            curr = next;
        }

        // Traverse mount point if it's mounted over
        if (curr->mount && curr->mount->root) {
            curr = curr->mount->root;
        }

        while (*p == '/') p++;
    }

    return curr;
}

struct inode *vfs_resolve(const char *path)
{
    struct dentry *d = path_walk(path);
    if (d) return d->inode;
    return NULL;
}

int vfs_mkdir(const char *path)
{
    if (!path || !*path) return -1;
    
    char tmp[256];
    uint32_t len = (uint32_t)strlen(path);
    if (len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);

    // simple dirname/basename split
    uint32_t slash = len;
    while (slash > 0 && tmp[slash - 1] != '/') slash--;
    
    char parent[256];
    if (slash == 0) {
        strcpy(parent, ".");
    } else if (slash == 1) {
        strcpy(parent, "/");
    } else {
        memcpy(parent, tmp, slash);
        parent[slash] = 0;
    }
    const char *name = tmp + slash;
    if (!*name) return -1;

    struct inode *pnode = vfs_resolve(parent);
    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY) return -1;
    if (!vfs_check_access(pnode, 0, 1, 1)) return -1;

    // Check if exists
    if (pnode->f_ops && pnode->f_ops->finddir && pnode->f_ops->finddir(pnode, name)) {
        return -1;
    }

    // In new generic VFS, ramfs_create_child just returns an inode.
    // We need to attach it to a dentry.
    struct dentry *pdentry = path_walk(parent);
    if (!pdentry) return -1;
    
    struct inode *created = ramfs_create_child(pnode, name, FS_DIRECTORY);
    if (!created) return -1;
    
    struct dentry *d = d_alloc(pdentry, name);
    if (!d) return -1;
    d->inode = created;

    return 0;
}
