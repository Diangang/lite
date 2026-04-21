#include "linux/fs.h"
#include "linux/namei.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/ramfs.h"
#include "linux/uaccess.h"

// Keep vfs_build_abs to build absolute path strings for simplicity
// Alternatively, we could do path_lookup from task cwd dentry without strings, but this is simpler
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

static int vfs_build_dentry_path(struct dentry *d, char *out, uint32_t cap)
{
    if (!d || !out || cap < 2)
        return 0;

    char tmp[512];
    if (cap > sizeof(tmp))
        cap = sizeof(tmp);
    uint32_t pos = cap - 1;
    tmp[pos] = 0;

    while (d) {
        if (d->mount && d->mount->root == d && d->mount->mountpoint) {
            d = d->mount->mountpoint;
            continue;
        }
        if (!d->parent || d->parent == d)
            break;
        uint32_t n = (uint32_t)strlen(d->name);
        if (n == 0 || pos < n + 1)
            return 0;
        pos -= n;
        memcpy(tmp + pos, d->name, n);
        pos--;
        tmp[pos] = '/';
        d = d->parent;
    }

    if (pos == cap - 1) {
        out[0] = '/';
        out[1] = 0;
        return 1;
    }
    memcpy(out, tmp + pos, cap - pos);
    return 1;
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

// Removed vfs_build_abs entirely, no longer needed as path lookup now runs
// through a minimal Linux-shaped nameidata/path_lookupat pipeline.

static void set_nameidata(struct nameidata *nd, int dfd, const char *name)
{
    memset(nd, 0, sizeof(*nd));
    nd->dfd = dfd;
    nd->name = name;
    if (current) {
        nd->saved = current->nameidata;
        current->nameidata = nd;
    }
}

static void restore_nameidata(struct nameidata *nd)
{
    if (current && current->nameidata == nd)
        current->nameidata = nd->saved;
}

static int path_init(struct nameidata *nd, const char *name, unsigned flags, const char **walk)
{
    struct dentry *root;
    struct dentry *pwd;

    if (!name || !walk)
        return -1;
    root = ((flags & LOOKUP_ROOT) && nd->root.dentry) ? nd->root.dentry : (current ? current->fs.root : NULL);
    pwd = ((flags & LOOKUP_ROOT) && nd->root.dentry) ? nd->root.dentry : (current ? current->fs.pwd : NULL);
    if (!root)
        root = vfs_root_dentry;
    if (!pwd)
        pwd = root;
    if (!root)
        return -1;

    nd->flags = flags;
    nd->name = name;
    nd->last = name;
    nd->last_type = LAST_ROOT;
    if (!(flags & LOOKUP_ROOT) || !nd->root.dentry) {
        nd->root.dentry = root;
        nd->root.mnt = root->mount ? root->mount : vfs_get_mounts();
    }

    if (name[0] == '/') {
        nd->path = nd->root;
        *walk = name;
        while (**walk == '/')
            (*walk)++;
        return 0;
    }

    if ((flags & LOOKUP_ROOT) && nd->root.dentry) {
        nd->path = nd->root;
        *walk = name;
        return 0;
    }

    if (nd->dfd != AT_FDCWD)
        return -1;

    nd->path.dentry = pwd;
    nd->path.mnt = pwd->mount ? pwd->mount : nd->root.mnt;
    *walk = name;
    return 0;
}

static int link_path_walk(const char *name, struct nameidata *nd)
{
    char cur_path[512];
    uint32_t plen = (uint32_t)strlen(name);
    if (plen >= sizeof(cur_path))
        return -1;
    memcpy(cur_path, name, plen + 1);

    for (int depth = 0; depth < 8; depth++) {
        struct dentry *curr = nd->path.dentry;
        struct vfsmount *curr_mnt = nd->path.mnt;
        const char *p = cur_path;

        while (*p == '/')
            p++;
        if (!*p)
            return 0;

        int restarted = 0;
        while (*p) {
            const char *s = p;
            while (*p && *p != '/')
                p++;
            uint32_t len = (uint32_t)(p - s);

            char part[128];
            if (len >= sizeof(part))
                return -1;
            memcpy(part, s, len);
            part[len] = 0;

            if (strcmp(part, ".") == 0) {
                nd->last_type = LAST_DOT;
            } else if (strcmp(part, "..") == 0) {
                nd->last_type = LAST_DOTDOT;
                if (curr->mount && curr->mount->root == curr && curr->mount->mountpoint) {
                    struct vfsmount *mnt = curr->mount;
                    struct dentry *mp = mnt->mountpoint;
                    curr_mnt = mnt->parent ? mnt->parent : nd->root.mnt;
                    curr = mp->parent ? mp->parent : mp;
                } else if (curr->parent) {
                    curr = curr->parent;
                }
            } else {
                struct dentry *next = d_lookup(curr, part);
                nd->last_type = LAST_NORM;
                if (next && (next->d_flags & DENTRY_NEGATIVE) &&
                    !(next->mount && next->mount->root))
                    return -1;
                if (!next && curr->inode) {
                    struct inode *child_inode = finddir_fs(curr->inode, part);
                    if (child_inode) {
                        next = d_alloc(curr, part);
                        if (next)
                            next->inode = child_inode;
                    } else {
                        next = d_alloc(curr, part);
                        if (next) {
                            next->inode = NULL;
                            next->d_flags |= DENTRY_NEGATIVE;
                        }
                    }
                }
                if (!next)
                    return -1;
                curr = next;
            }

            if (curr->mount && curr->mount->root) {
                curr_mnt = curr->mount;
                curr = curr->mount->root;
            }

            if (curr->inode && curr->inode->flags == FS_SYMLINK &&
                curr->inode->f_ops && curr->inode->f_ops->read) {
                char target[256];
                uint32_t n = curr->inode->f_ops->read(curr->inode, 0,
                                                      sizeof(target) - 1,
                                                      (uint8_t *)target);
                if (n == 0 || n >= sizeof(target))
                    return -1;
                target[n] = 0;

                const char *rest = p;
                while (*rest == '/')
                    rest++;

                char new_path[512];
                uint32_t off = 0;
                if (target[0] == '/') {
                    uint32_t tlen = (uint32_t)strlen(target);
                    if (tlen + 1 > sizeof(new_path))
                        return -1;
                    memcpy(new_path, target, tlen);
                    off = tlen;
                } else {
                    char base_path[256];
                    uint32_t blen = 0;
                    if (cur_path[0] == '/') {
                        const char *base_end = s;
                        blen = (uint32_t)(base_end - cur_path);
                        if (blen == 0) {
                            base_path[0] = '/';
                            base_path[1] = 0;
                            blen = 1;
                        } else {
                            while (blen > 1 && cur_path[blen - 1] == '/')
                                blen--;
                            if (blen >= sizeof(base_path))
                                return -1;
                            memcpy(base_path, cur_path, blen);
                            base_path[blen] = 0;
                        }
                    } else {
                        struct dentry *base = curr->parent ? curr->parent : curr;
                        if (!vfs_build_dentry_path(base, base_path, sizeof(base_path)))
                            return -1;
                        blen = (uint32_t)strlen(base_path);
                    }
                    {
                        uint32_t tlen = (uint32_t)strlen(target);
                        if (blen + 1 + tlen + 1 > sizeof(new_path))
                            return -1;
                        memcpy(new_path, base_path, blen);
                        off = blen;
                        if (off && new_path[off - 1] != '/')
                            new_path[off++] = '/';
                        memcpy(new_path + off, target, tlen);
                        off += tlen;
                    }
                }

                uint32_t rlen = (uint32_t)strlen(rest);
                if (rlen) {
                    if (off && new_path[off - 1] != '/')
                        new_path[off++] = '/';
                    memcpy(new_path + off, rest, rlen);
                    off += rlen;
                }
                new_path[off] = 0;
                if (!vfs_normalize_path(new_path, cur_path, sizeof(cur_path)))
                    return -1;
                restarted = 1;
                if (cur_path[0] == '/') {
                    nd->path = nd->root;
                } else {
                    nd->path.dentry = curr->parent ? curr->parent : curr;
                    nd->path.mnt = curr_mnt;
                }
                break;
            }

            while (*p == '/')
                p++;
        }

        if (!restarted) {
            nd->path.dentry = curr;
            nd->path.mnt = curr_mnt;
            return 0;
        }
    }

    return -1;
}

static int path_lookupat(struct nameidata *nd, unsigned flags, struct path *path)
{
    const char *walk;

    if (path_init(nd, nd->name, flags, &walk) != 0)
        return -1;
    if (*walk && link_path_walk(walk, nd) != 0)
        return -1;
    if ((flags & LOOKUP_DIRECTORY) &&
        (!nd->path.dentry || !nd->path.dentry->inode ||
         (nd->path.dentry->inode->flags & 0x7) != FS_DIRECTORY))
        return -1;
    *path = nd->path;
    return 0;
}

int filename_lookup(int dfd, const char *name, unsigned flags, struct path *path)
{
    struct nameidata nd;
    int ret;

    if (!name || !*name || !path)
        return -1;
    set_nameidata(&nd, dfd, name);
    ret = path_lookupat(&nd, flags, path);
    restore_nameidata(&nd);
    return ret;
}

int kern_path(const char *name, unsigned flags, struct path *path)
{
    return filename_lookup(AT_FDCWD, name, flags, path);
}

int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
                    const char *name, unsigned flags, struct path *path)
{
    struct nameidata nd;
    int ret;

    if (!dentry || !mnt || !name || !*name || !path)
        return -1;
    set_nameidata(&nd, AT_FDCWD, name);
    nd.root.dentry = dentry;
    nd.root.mnt = mnt;
    ret = path_lookupat(&nd, flags | LOOKUP_ROOT, path);
    restore_nameidata(&nd);
    return ret;
}

static int path_parentat(struct nameidata *nd, const char *parent_name,
                         unsigned flags, struct path *parent)
{
    const char *saved_name;
    int ret;

    if (!nd || !parent_name || !parent)
        return -1;
    saved_name = nd->name;
    nd->name = parent_name;
    ret = path_lookupat(nd, flags | LOOKUP_DIRECTORY, parent);
    nd->name = saved_name;
    return ret;
}

int filename_parentat(int dfd, const char *name, unsigned flags,
                      struct path *parent, char *last, uint32_t last_cap,
                      int *type)
{
    struct nameidata nd;
    char parent_name[256];
    uint32_t len;
    uint32_t slash;
    uint32_t base_len;
    int ret;

    if (!name || !*name || !parent || !last || last_cap == 0 || !type)
        return -1;

    len = (uint32_t)strlen(name);
    while (len > 1 && name[len - 1] == '/')
        len--;
    if (len == 0)
        return -1;

    slash = len;
    while (slash > 0 && name[slash - 1] != '/')
        slash--;

    if (slash == 0) {
        strcpy(parent_name, ".");
        base_len = len;
        if (base_len >= last_cap)
            return -1;
        memcpy(last, name, base_len);
        last[base_len] = 0;
    } else if (slash == 1) {
        strcpy(parent_name, "/");
        base_len = len - 1;
        if (base_len == 0 || base_len >= last_cap)
            return -1;
        memcpy(last, name + 1, base_len);
        last[base_len] = 0;
    } else {
        if (slash >= sizeof(parent_name))
            return -1;
        memcpy(parent_name, name, slash - 1);
        parent_name[slash - 1] = 0;
        base_len = len - slash;
        if (base_len == 0 || base_len >= last_cap)
            return -1;
        memcpy(last, name + slash, base_len);
        last[base_len] = 0;
    }

    if (strcmp(last, ".") == 0)
        *type = LAST_DOT;
    else if (strcmp(last, "..") == 0)
        *type = LAST_DOTDOT;
    else
        *type = LAST_NORM;

    set_nameidata(&nd, dfd, name);
    nd.last = last;
    nd.last_type = *type;
    ret = path_parentat(&nd, parent_name, flags, parent);
    restore_nameidata(&nd);
    return ret;
}

int vfs_symlink(const char *target, const char *linkpath)
{
    struct path parent;
    char name[128];
    int last_type;

    if (!target || !linkpath)
        return -1;

    if (filename_parentat(AT_FDCWD, linkpath, 0, &parent, name, sizeof(name), &last_type) != 0)
        return -1;
    if (last_type != LAST_NORM)
        return -1;
    if (!parent.dentry || !parent.dentry->inode ||
        (parent.dentry->inode->flags & 0x7) != FS_DIRECTORY)
        return -1;

    struct inode *created = symlink_fs(parent.dentry->inode, name, target);
    if (!created)
        return -1;

    struct dentry *d = d_lookup(parent.dentry, name);
    if (d) {
        d->inode = created;
        d->d_flags &= ~DENTRY_NEGATIVE;
    } else {
        d = d_alloc(parent.dentry, name);
        if (!d)
            return -1;
        d->inode = created;
    }

    return 0;
}

/* vfs_resolve: Implement vfs resolve. */
struct inode *vfs_resolve(const char *path)
{
    struct path lookup;

    if (kern_path(path, 0, &lookup) != 0)
        return NULL;
    return lookup.dentry ? lookup.dentry->inode : NULL;
}

static char *find_slash(char *s)
{
    while (s && *s) {
        if (*s == '/')
            return s;
        s++;
    }
    return NULL;
}

static struct dentry *vfs_lookup_child(struct dentry *parent, const char *name)
{
    struct dentry *child;
    struct inode *inode;

    if (!parent || !name || !name[0])
        return NULL;
    child = d_lookup(parent, name);
    if (child)
        return child;
    if (!parent->inode)
        return NULL;
    inode = finddir_fs(parent->inode, name);
    if (!inode)
        return NULL;
    child = d_alloc(parent, name);
    if (!child)
        return NULL;
    child->inode = inode;
    return child;
}

static struct dentry *vfs_walk_parent_at(struct dentry *root, const char *path,
                                         char *leaf, uint32_t leaf_cap,
                                         int create_dirs)
{
    char tmp[256];
    char *p;
    struct dentry *curr = root;

    if (!root || !path || !path[0] || !leaf || leaf_cap == 0)
        return NULL;
    if (strlen(path) >= sizeof(tmp))
        return NULL;
    strcpy(tmp, path);

    p = tmp;
    for (;;) {
        char *slash = find_slash(p);
        struct dentry *child;
        if (!slash)
            break;
        *slash = 0;
        if (!p[0])
            return NULL;
        child = vfs_lookup_child(curr, p);
        if (!child && create_dirs) {
            struct inode *inode = mkdir_fs(curr->inode, p);
            if (!inode)
                return NULL;
            child = d_alloc(curr, p);
            if (!child)
                return NULL;
            child->inode = inode;
        }
        if (!child || !child->inode || (child->inode->flags & 0x7) != FS_DIRECTORY)
            return NULL;
        curr = child;
        p = slash + 1;
    }

    if (!p[0] || strlen(p) >= leaf_cap)
        return NULL;
    strcpy(leaf, p);
    return curr;
}

struct inode *vfs_resolve_at(struct dentry *root, const char *path)
{
    char leaf[128];
    struct dentry *parent;
    struct dentry *child;

    parent = vfs_walk_parent_at(root, path, leaf, sizeof(leaf), 0);
    if (!parent)
        return NULL;
    child = vfs_lookup_child(parent, leaf);
    return child ? child->inode : NULL;
}

int vfs_create_path_at(struct dentry *root, const char *path)
{
    char leaf[128];
    return vfs_walk_parent_at(root, path, leaf, sizeof(leaf), 1) ? 0 : -1;
}

static int vfs_attach_inode_at(struct dentry *root, const char *path, struct inode *inode)
{
    char name[128];
    struct dentry *pdentry;
    struct dentry *d;

    if (!inode)
        return -1;
    pdentry = vfs_walk_parent_at(root, path, name, sizeof(name), 0);
    if (!pdentry)
        return -1;

    d = vfs_lookup_child(pdentry, name);

    if (d) {
        if (d->inode)
            return 0;
        d->inode = inode;
        d->d_flags &= ~DENTRY_NEGATIVE;
        return 0;
    }

    d = d_alloc(pdentry, name);
    if (!d)
        return -1;
    d->inode = inode;
    return 0;
}

int vfs_mknod_at(struct dentry *root, const char *path, uint32_t type, dev_t devt,
                 void *private_data, uint32_t mode, uint32_t uid, uint32_t gid)
{
    struct inode *inode = create_special_inode(type, devt, private_data, mode, uid, gid);
    int ret;

    if (!inode)
        return -1;
    ret = vfs_attach_inode_at(root, path, inode);
    if (ret != 0)
        destroy_special_inode(inode);
    return ret;
}

static struct inode *vfs_detach_inode_at(struct dentry *root, const char *path)
{
    char name[128];
    struct dentry *pdentry;
    struct dentry *d;
    struct inode *inode;

    pdentry = vfs_walk_parent_at(root, path, name, sizeof(name), 0);
    if (!pdentry)
        return NULL;

    d = vfs_lookup_child(pdentry, name);
    if (!d || !d->inode)
        return NULL;

    inode = d->inode;
    d->inode = NULL;
    vfs_dentry_detach(d);
    vfs_dentry_put(d);
    return inode;
}

int vfs_unlink_at(struct dentry *root, const char *path)
{
    struct inode *inode = vfs_detach_inode_at(root, path);

    if (!inode)
        return -1;
    destroy_special_inode(inode);
    return 0;
}

int vfs_rmdir_at(struct dentry *root, const char *path)
{
    char name[128];
    struct dentry *pdentry = vfs_walk_parent_at(root, path, name, sizeof(name), 0);

    if (!pdentry || !pdentry->inode || (pdentry->inode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    return rmdir_fs(pdentry, name);
}

/* vfs_mkdir: Implement vfs mkdir. */
int vfs_mkdir(const char *path)
{
    struct path parent;
    char name[128];
    int last_type;

    if (!path || !*path)
        return -1;
    if (filename_parentat(AT_FDCWD, path, 0, &parent, name, sizeof(name), &last_type) != 0)
        return -1;
    if (last_type != LAST_NORM)
        return -1;

    struct inode *pnode = parent.dentry ? parent.dentry->inode : NULL;
    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(pnode, 0, 1, 1))
        return -1;

    // Check if exists
    if (finddir_fs(pnode, name))
        return -1;

    if (!parent.dentry)
        return -1;

    struct inode *created = mkdir_fs(pnode, name);
    if (!created)
        return -1;

    struct dentry *d = d_lookup(parent.dentry, name);
    if (d) {
        d->inode = created;
        d->d_flags &= ~DENTRY_NEGATIVE;
    } else {
        d = d_alloc(parent.dentry, name);
        if (!d)
            return -1;
        d->inode = created;
    }

    return 0;
}

/* vfs_unlink: Implement vfs unlink. */
int vfs_unlink(const char *path)
{
    struct path parent;
    char name[128];
    int last_type;

    if (!path || !*path)
        return -1;
    if (filename_parentat(AT_FDCWD, path, 0, &parent, name, sizeof(name), &last_type) != 0)
        return -1;
    if (last_type != LAST_NORM)
        return -1;
    if (!parent.dentry)
        return -1;
    struct inode *pnode = parent.dentry->inode;

    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(pnode, 0, 1, 1))
        return -1;

    int ret = unlink_fs(parent.dentry, name);
    if (ret == 0) {
        struct dentry *child = d_lookup(parent.dentry, name);
        if (child) {
            child->inode = NULL;
            child->d_flags |= DENTRY_NEGATIVE;
        }
    }
    return ret;
}

/* vfs_rmdir: Implement vfs rmdir. */
int vfs_rmdir(const char *path)
{
    struct path parent;
    char name[128];
    int last_type;

    if (!path || !*path)
        return -1;
    if (filename_parentat(AT_FDCWD, path, 0, &parent, name, sizeof(name), &last_type) != 0)
        return -1;
    if (last_type != LAST_NORM)
        return -1;
    if (!parent.dentry)
        return -1;
    struct inode *pnode = parent.dentry->inode;

    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(pnode, 0, 1, 1))
        return -1;

    int ret = rmdir_fs(parent.dentry, name);
    if (ret == 0) {
        struct dentry *child = d_lookup(parent.dentry, name);
        if (child) {
            /* Drop stale subtree so future lookups do not observe old entries. */
            while (child->children) {
                struct dentry *c = child->children;
                vfs_dentry_detach(c);
                vfs_dentry_put(c);
            }
            child->inode = NULL;
            child->d_flags |= DENTRY_NEGATIVE;
        }
    }
    return ret;
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
