#include "linux/fs.h"
#include "linux/libc.h"
#include "linux/sched.h"
#include "linux/ramfs.h"
#include "linux/uaccess.h"

// Keep vfs_build_abs to build absolute path strings for simplicity
// Alternatively, we could do path_walk from task cwd dentry without strings, but this is simpler
// because task struct currently stores cwd as string.
static int vfs_append_component(char *out, uint32_t *len, uint32_t cap, const char *start, uint32_t n)
{
    if (!out || !len || !start)
        return 0;
    if (*len == 0) {
        if (cap < 2)
            return 0;
        out[0] = '/';
        out[1] = 0;
        *len = 1;
    }
    if (n == 0)
        return 1;
    if (*len + 1 >= cap)
        return 0;
    if (out[*len - 1] != '/') {
        out[*len] = '/';
        (*len)++;
        out[*len] = 0;
    }
    if (*len + n >= cap)
        return 0;
    memcpy(out + *len, start, n);
    *len += n;
    out[*len] = 0;
    return 1;
}

/* vfs_pop_component: Implement vfs pop component. */
static void vfs_pop_component(char *out, uint32_t *len)
{
    if (!out || !len)
        return;
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

/* vfs_normalize_path: Implement vfs normalize path. */
int vfs_normalize_path(const char *path, char *out, uint32_t cap)
{
    if (!path || !out || cap < 2)
        return 0;
    uint32_t len = 0;
    out[0] = 0;
    const char *p = path;
    while (*p == '/') p++;
    if (!vfs_append_component(out, &len, cap, "", 0))
        return 0;

    while (*p) {
        const char *s = p;
        while (*p && *p != '/') p++;
        uint32_t n = (uint32_t)(p - s);
        if (n == 1 && s[0] == '.') {
            // skip
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            vfs_pop_component(out, &len);
        } else if (n > 0) {
            if (!vfs_append_component(out, &len, cap, s, n))
                return 0;
        }
        while (*p == '/') p++;
    }
    return 1;
}

// Removed vfs_build_abs entirely, no longer needed as path_walk now walks relative to dentry

struct dentry *path_walk(const char *path)
{
    if (!path || !*path)
        return NULL;

    /* Minimal symlink following:
     * - Only supports absolute symlink targets (which is sufficient for sysfs links).
     * - Caps recursion depth to avoid loops.
     */
    char cur_path[512];
    uint32_t plen = (uint32_t)strlen(path);
    if (plen >= sizeof(cur_path))
        return NULL;
    memcpy(cur_path, path, plen + 1);

    for (int depth = 0; depth < 8; depth++) {
        struct dentry *curr;
        if (cur_path[0] == '/') {
            curr = current ? current->fs.root : NULL;
            if (!curr)
                curr = vfs_root_dentry;
        } else {
            curr = current ? current->fs.pwd : NULL;
            if (!curr)
                curr = vfs_root_dentry;
        }

        if (!curr)
            return NULL;

        const char *p = cur_path;
        while (*p == '/') p++;

        int restarted = 0;
        while (*p) {
            const char *s = p;
            while (*p && *p != '/') p++;
            uint32_t len = (uint32_t)(p - s);

            char part[128];
            if (len >= sizeof(part))
                return NULL;
            memcpy(part, s, len);
            part[len] = 0;

            if (strcmp(part, ".") == 0) {
                // curr stays the same
            } else if (strcmp(part, "..") == 0) {
                if (curr->parent)
                    curr = curr->parent;
            } else {
                struct dentry *next = d_lookup(curr, part);
                if (!next) {
                    /* Not in cache, try underlying FS. */
                    if (curr->inode && curr->inode->f_ops && curr->inode->f_ops->finddir) {
                        struct inode *child_inode = curr->inode->f_ops->finddir(curr->inode, part);
                        if (child_inode) {
                            next = d_alloc(curr, part);
                            next->inode = child_inode;
                        }
                    }
                }
                if (!next)
                    return NULL; // Not found
                curr = next;
            }

            /* Traverse mount point if it's mounted over. */
            if (curr->mount && curr->mount->root)
                curr = curr->mount->root;

            /* If this component resolves to a symlink, follow it. */
            if (curr->inode && (curr->inode->flags == FS_SYMLINK) && curr->inode->f_ops && curr->inode->f_ops->read) {
                char target[256];
                uint32_t n = curr->inode->f_ops->read(curr->inode, 0, sizeof(target) - 1, (uint8_t *)target);
                if (n == 0 || n >= sizeof(target))
                    return NULL;
                target[n] = 0;
                if (target[0] != '/')
                    return NULL; /* Only absolute targets supported. */

                const char *rest = p;
                while (*rest == '/')
                    rest++;

                char new_path[512];
                uint32_t tlen = (uint32_t)strlen(target);
                uint32_t rlen = (uint32_t)strlen(rest);
                if (tlen + 1 + rlen + 1 > sizeof(new_path))
                    return NULL;

                memcpy(new_path, target, tlen);
                uint32_t off = tlen;
                if (rlen) {
                    if (off && new_path[off - 1] != '/')
                        new_path[off++] = '/';
                    memcpy(new_path + off, rest, rlen);
                    off += rlen;
                }
                new_path[off] = 0;

                memcpy(cur_path, new_path, off + 1);
                restarted = 1;
                break;
            }

            while (*p == '/') p++;
        }

        if (!restarted)
            return curr;
    }

    return NULL;
}

/* vfs_resolve: Implement vfs resolve. */
struct inode *vfs_resolve(const char *path)
{
    struct dentry *d = path_walk(path);
    if (d)
        return d->inode;
    return NULL;
}

/* vfs_mkdir: Implement vfs mkdir. */
int vfs_mkdir(const char *path)
{
    if (!path || !*path)
        return -1;

    char tmp[256];
    uint32_t len = (uint32_t)strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    strcpy(tmp, path);

    uint32_t slash = len;
    int found_slash = 0;
    while (slash > 0) {
        if (tmp[slash - 1] == '/') {
            found_slash = 1;
            break;
        }
        slash--;
    }

    char parent[256];
    const char *name;
    if (!found_slash) {
        // e.g. "test.txt" -> parent is ".", name is "test.txt"
        strcpy(parent, ".");
        name = tmp;
    } else if (slash == 1) {
        // e.g. "/test.txt" -> parent is "/", name is "test.txt"
        strcpy(parent, "/");
        name = tmp + 1;
    } else {
        // e.g. "/bin/sh" -> parent is "/bin", name is "sh"
        memcpy(parent, tmp, slash - 1); // Exclude the trailing slash
        parent[slash - 1] = 0;
        name = tmp + slash;
    }
    if (!*name)
        return -1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    struct inode *pnode = vfs_resolve(parent);
    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(pnode, 0, 1, 1))
        return -1;

    // Check if exists
    if (pnode->f_ops && pnode->f_ops->finddir && pnode->f_ops->finddir(pnode, name))
        return -1;

    // In new generic VFS, ramfs_create_child just returns an inode.
    // We need to attach it to a dentry.
    struct dentry *pdentry = path_walk(parent);
    if (!pdentry)
        return -1;

    struct inode *created = ramfs_create_child(pnode, name, FS_DIRECTORY);
    if (!created)
        return -1;

    struct dentry *d = d_alloc(pdentry, name);
    if (!d)
        return -1;
    d->inode = created;

    return 0;
}

/* vfs_unlink: Implement vfs unlink. */
int vfs_unlink(const char *path)
{
    if (!path || !*path)
        return -1;

    char tmp[256];
    uint32_t len = (uint32_t)strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    strcpy(tmp, path);

    uint32_t slash = len;
    int found_slash = 0;
    while (slash > 0) {
        if (tmp[slash - 1] == '/') {
            found_slash = 1;
            break;
        }
        slash--;
    }

    char parent[256];
    const char *name;
    if (!found_slash) {
        // e.g. "test.txt" -> parent is ".", name is "test.txt"
        strcpy(parent, ".");
        name = tmp;
    } else if (slash == 1) {
        // e.g. "/test.txt" -> parent is "/", name is "test.txt"
        strcpy(parent, "/");
        name = tmp + 1;
    } else {
        // e.g. "/bin/sh" -> parent is "/bin", name is "sh"
        memcpy(parent, tmp, slash - 1);
        parent[slash - 1] = 0;
        name = tmp + slash;
    }
    if (!*name)
        return -1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    struct dentry *pdentry = path_walk(parent);
    if (!pdentry)
        return -1;
    struct inode *pnode = pdentry->inode;

    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(pnode, 0, 1, 1))
        return -1;

    if (pnode->f_ops && pnode->f_ops->unlink)
        return pnode->f_ops->unlink(pdentry, name);

    return -1;
}

/* vfs_rmdir: Implement vfs rmdir. */
int vfs_rmdir(const char *path)
{
    if (!path || !*path)
        return -1;

    char tmp[256];
    uint32_t len = (uint32_t)strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    strcpy(tmp, path);

    uint32_t slash = len;
    int found_slash = 0;
    while (slash > 0) {
        if (tmp[slash - 1] == '/') {
            found_slash = 1;
            break;
        }
        slash--;
    }

    char parent[256];
    const char *name;
    if (!found_slash) {
        strcpy(parent, ".");
        name = tmp;
    } else if (slash == 1) {
        strcpy(parent, "/");
        name = tmp + 1;
    } else {
        memcpy(parent, tmp, slash - 1);
        parent[slash - 1] = 0;
        name = tmp + slash;
    }
    if (!*name)
        return -1;

    struct dentry *pdentry = path_walk(parent);
    if (!pdentry)
        return -1;
    struct inode *pnode = pdentry->inode;

    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(pnode, 0, 1, 1))
        return -1;

    if (pnode->f_ops && pnode->f_ops->rmdir)
        return pnode->f_ops->rmdir(pdentry, name);

    return -1;
}

/* sys_mkdir: Implement sys mkdir. */
int sys_mkdir(const char *pathname, int from_user)
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
    return vfs_mkdir(tmp) == 0 ? 0 : -1;
}

/* sys_unlink: Implement sys unlink. */
int sys_unlink(const char *pathname, int from_user)
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
    return vfs_unlink(tmp);
}

/* sys_rmdir: Implement sys rmdir. */
int sys_rmdir(const char *pathname, int from_user)
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
    return vfs_rmdir(tmp);
}
