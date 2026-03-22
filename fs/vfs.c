#include "vfs.h"
#include "kheap.h"
#include "libc.h"
#include "ramfs.h"
#include "task.h"

static struct vfs_mount vfs_mounts[8];
static uint32_t vfs_mount_count = 0;
static struct vfs_inode vfs_root_node;
static struct vfs_inode *vfs_base_root = NULL;
static struct dirent vfs_root_dirent;
static struct vfs_dentry *vfs_root_dentry = NULL;
static char vfs_boot_cwd[128];
struct vfs_icache_entry {
    struct vfs_inode *node;
    struct vfs_inode inode;
    struct vfs_dentry dentry;
    uint32_t refs;
    struct vfs_icache_entry *next;
};
static struct vfs_icache_entry *vfs_icache_head = NULL;

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

static struct vfs_dentry *vfs_dentry_get(struct vfs_inode *node, const char *name)
{
    if (!node) return NULL;
    struct vfs_icache_entry *e = vfs_icache_head;
    while (e) {
        if (e->node == node) {
            e->refs++;
            e->inode.refcount++;
            e->dentry.refcount++;
            return &e->dentry;
        }
        e = e->next;
    }
    e = (struct vfs_icache_entry*)kmalloc(sizeof(struct vfs_icache_entry));
    if (!e) return NULL;
    memset(e, 0, sizeof(*e));
    e->node = node;
    e->inode.f_ops = node->f_ops;
    e->inode.private_data = node->private_data;
    e->inode.mask = node->mask;
    e->inode.uid = node->uid;
    e->inode.gid = node->gid;
    e->inode.flags = node->flags;
    e->inode.inode = node->inode;
    e->inode.length = node->length;
    e->inode.impl = node->impl;
    e->dentry.name = name ? strdup(name) : NULL;
    e->dentry.parent = NULL;
    e->dentry.inode = &e->inode;
    e->dentry.refcount = 1;
    e->dentry.cache = e;
    e->refs = 1;
    e->next = vfs_icache_head;
    vfs_icache_head = e;
    return &e->dentry;
}

static void vfs_dentry_put(struct vfs_dentry *d)
{
    if (!d) return;
    struct vfs_icache_entry *e = (struct vfs_icache_entry*)d->cache;
    if (!e) return;
    if (e->refs > 0) e->refs--;
    if (e->inode.refcount > 0) e->inode.refcount--;
    if (e->dentry.refcount > 0) e->dentry.refcount--;
    if (e->refs > 0) return;
    struct vfs_icache_entry **pp = &vfs_icache_head;
    while (*pp) {
        if (*pp == e) {
            *pp = e->next;
            kfree(e);
            return;
        }
        pp = &(*pp)->next;
    }
}

static struct vfs_file_operations vfs_root_ops;

void vfs_init(void)
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
    // Don't overwrite initrd_root, let initrd keep it or use a separate variable
    // Wait, initrd_root is used for the root directory of the whole system?
    // Let's just set vfs_root_node.f_ops
    vfs_base_root = NULL;
    strcpy(vfs_boot_cwd, "/");
}

static int vfs_path_is_prefix(const char *path, const char *prefix, uint32_t *out_tail_off)
{
    if (!path || !prefix) return 0;
    if (prefix[0] == '/' && prefix[1] == 0) {
        if (path[0] != '/') return 0;
        if (out_tail_off) *out_tail_off = 1;
        return 1;
    }
    uint32_t i = 0;
    while (prefix[i]) {
        if (path[i] != prefix[i]) return 0;
        i++;
    }
    if (path[i] == 0) {
        if (out_tail_off) *out_tail_off = i;
        return 1;
    }
    if (path[i] == '/') {
        if (out_tail_off) *out_tail_off = i + 1;
        return 1;
    }
    return 0;
}

static int vfs_append_component(char *out, uint32_t *len, uint32_t cap, const char *start, uint32_t n)
{
    if (!out || !len || !start) return 0;
    if (n == 0) return 1;
    if (*len == 0) {
        if (cap < 2) return 0;
        out[0] = '/';
        out[1] = 0;
        *len = 1;
    }
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
    while (*len > 1 && out[*len - 1] == '/') {
        out[*len - 1] = 0;
        (*len)--;
    }
    while (*len > 1 && out[*len - 1] != '/') {
        out[*len - 1] = 0;
        (*len)--;
    }
    while (*len > 1 && out[*len - 1] == '/') {
        out[*len - 1] = 0;
        (*len)--;
    }
    if (*len == 0) {
        out[0] = '/';
        out[1] = 0;
        *len = 1;
    }
}

static int vfs_normalize_path(const char *path, char *out, uint32_t cap)
{
    if (!path || !out || cap < 2) return 0;
    out[0] = '/';
    out[1] = 0;
    uint32_t out_len = 1;

    const char *p = path;
    while (*p == '/') p++;

    while (*p) {
        const char *seg = p;
        while (*p && *p != '/') p++;
        uint32_t n = (uint32_t)(p - seg);
        while (*p == '/') p++;

        if (n == 0) continue;
        if (n == 1 && seg[0] == '.') continue;
        if (n == 2 && seg[0] == '.' && seg[1] == '.') {
            vfs_pop_component(out, &out_len);
            continue;
        }
        if (!vfs_append_component(out, &out_len, cap, seg, n)) return 0;
    }

    if (out_len == 0) {
        out[0] = '/';
        out[1] = 0;
    }
    return 1;
}

static int vfs_build_abs(const char *path, char *abs, uint32_t cap)
{
    if (!path || !abs || cap < 2) return 0;
    if (path[0] == '/') return vfs_normalize_path(path, abs, cap);

    char tmp[256];
    uint32_t off = 0;
    const char *cwd = task_get_cwd();
    if (!cwd || !*cwd) cwd = vfs_boot_cwd;
    uint32_t cwd_len = (uint32_t)strlen(cwd);
    if (cwd_len >= sizeof(tmp) - 1) return 0;
    memcpy(tmp, cwd, cwd_len);
    off = cwd_len;
    if (off == 0) {
        tmp[off++] = '/';
    } else if (tmp[off - 1] != '/') {
        if (off + 1 >= sizeof(tmp)) return 0;
        tmp[off++] = '/';
    }
    tmp[off] = 0;

    uint32_t path_len = (uint32_t)strlen(path);
    if (off + path_len >= sizeof(tmp)) return 0;
    memcpy(tmp + off, path, path_len);
    off += path_len;
    tmp[off] = 0;
    return vfs_normalize_path(tmp, abs, cap);
}

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
    return readdir_fs(vfs_base_root, index - n_mounts + 2); // Skip . and .. from base root
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
    return finddir_fs(vfs_base_root, name);
}

static struct vfs_file_operations vfs_root_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = vfs_root_readdir,
    .finddir = vfs_root_finddir,
    .ioctl = NULL
};

int vfs_mount_root(const char *path, struct vfs_inode *root_node)
{
    if (!path || !root_node) return -1;
    if (vfs_mount_count >= (sizeof(vfs_mounts) / sizeof(vfs_mounts[0]))) return -1;

    struct vfs_mount *m = &vfs_mounts[vfs_mount_count++];
    m->path = path;
    m->sb = (struct vfs_super_block*)kmalloc(sizeof(struct vfs_super_block));
    if (!m->sb) return -1;
    m->sb->name = path;
    m->sb->refcount = 1;
    if (path[0] == '/' && path[1] == 0) {
        vfs_base_root = root_node;
        vfs_root_node.f_ops = &vfs_root_ops;
        m->sb->root = &vfs_root_node;
    } else {
        m->sb->root = root_node;
    }
    m->sb->fs_private = NULL;
    return 0;
}

static struct vfs_mount *vfs_find_mount(const char *path, uint32_t *out_tail_off)
{
    struct vfs_mount *best = NULL;
    uint32_t best_len = 0;
    uint32_t tail = 0;

    for (uint32_t i = 0; i < vfs_mount_count; i++) {
        struct vfs_mount *m = &vfs_mounts[i];
        if (!m->path) continue;
        uint32_t off = 0;
        if (!vfs_path_is_prefix(path, m->path, &off)) continue;
        uint32_t len = (uint32_t)strlen(m->path);
        if (len >= best_len) {
            best = m;
            best_len = len;
            tail = off;
        }
    }
    if (out_tail_off) *out_tail_off = tail;
    return best;
}

int vfs_mkdir(const char *path)
{
    if (!path || !*path) return -1;
    char abs[256];
    if (!vfs_build_abs(path, abs, sizeof(abs))) return -1;
    uint32_t abs_len = (uint32_t)strlen(abs);
    if (abs_len < 2) return -1;
    uint32_t slash = abs_len;
    while (slash > 0 && abs[slash - 1] != '/') slash--;
    if (slash == 0 || slash >= abs_len) return -1;
    char parent[256];
    if (slash == 1) {
        strcpy(parent, "/");
    } else {
        memcpy(parent, abs, slash);
        parent[slash] = 0;
    }
    const char *name = abs + slash;
    if (!*name) return -1;

    struct vfs_inode *pnode = vfs_resolve(parent);
    if (pnode == &vfs_root_node) pnode = vfs_base_root;
    if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY) return -1;
    if (!vfs_check_access(pnode, 0, 1, 1)) return -1;
    if (finddir_fs(pnode, (char*)name)) return -1;
    if (!ramfs_create_child(pnode, name, FS_DIRECTORY)) return -1;
    return 0;
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

struct vfs_inode *vfs_resolve(const char *path)
{
    if (!path || !*path) return NULL;
    char abs[256];
    if (!vfs_build_abs(path, abs, sizeof(abs))) return NULL;
    uint32_t tail = 0;
    struct vfs_mount *m = vfs_find_mount(abs, &tail);
    if (!m || !m->sb || !m->sb->root) return NULL;
    const char *sub = abs + tail;
    while (*sub == '/') sub++;
    if (*sub == 0) return m->sb->root;
    
    // Check if the mount point is the root mount, and if so we just search
    if (m == &vfs_mounts[0]) {
        if (!vfs_base_root) return NULL;
        return finddir_fs(vfs_base_root, (char*)sub);
    }
    
    struct vfs_inode *node = finddir_fs(m->sb->root, (char*)sub);
    return node;
}

struct vfs_file *vfs_open(const char *path, uint32_t flags)
{
    struct vfs_inode *node = vfs_resolve(path);
    if (!node && (flags & VFS_O_CREAT)) {
        char abs[256];
        if (!vfs_build_abs(path, abs, sizeof(abs))) return NULL;
        char parent[256];
        uint32_t abs_len = (uint32_t)strlen(abs);
        if (abs_len < 2) return NULL;
        uint32_t slash = abs_len;
        while (slash > 0 && abs[slash - 1] != '/') slash--;
        if (slash == 0 || slash >= abs_len) return NULL;
        if (slash == 1) {
            strcpy(parent, "/");
        } else {
            memcpy(parent, abs, slash);
            parent[slash] = 0;
        }
        const char *name = abs + slash;
        if (!*name) return NULL;

        struct vfs_inode *pnode = vfs_resolve(parent);
        if (pnode == &vfs_root_node) pnode = vfs_base_root;
        if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY) return NULL;
        if (!vfs_check_access(pnode, 0, 1, 1)) return NULL;
        struct vfs_inode *created = ramfs_create_child(pnode, name, FS_FILE);
        if (!created) return NULL;
        node = created;
    }
    if (!node) return NULL;
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        if (!vfs_check_access(node, 1, 0, 1)) return NULL;
    } else {
        int need_write = (flags & VFS_O_TRUNC) ? 1 : 0;
        if (!vfs_check_access(node, 1, need_write, 0)) return NULL;
    }

    struct vfs_file *f = vfs_open_node(node, flags);
    if (!f) return NULL;
    if ((flags & VFS_O_TRUNC) && (node->flags & 0x7) == FS_FILE) {
        node->length = 0;
    }
    return f;
}

struct vfs_file *vfs_open_node(struct vfs_inode *node, uint32_t flags)
{
    if (!node) return NULL;
    struct vfs_file *f = (struct vfs_file*)kmalloc(sizeof(struct vfs_file));
    if (!f) {
        if (f) kfree(f);
        return NULL;
    }
    struct vfs_dentry *d = vfs_dentry_get(node, "");
    if (!d) {
        kfree(f);
        return NULL;
    }
    f->dentry = d;
    f->pos = 0;
    f->flags = flags;
    f->refcount = 1;
    open_fs(node, 1, 1);
    return f;
}

uint32_t vfs_read(struct vfs_file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode) return 0;
    if (!vfs_check_access(f->dentry->inode, 1, 0, 0)) return 0;
    uint32_t n = read_fs(f->dentry->inode, f->pos, len, buf);
    f->pos += n;
    return n;
}

uint32_t vfs_write(struct vfs_file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode) return 0;
    if (!vfs_check_access(f->dentry->inode, 0, 1, 0)) return 0;
    uint32_t n = write_fs(f->dentry->inode, f->pos, len, (uint8_t*)buf);
    f->pos += n;
    return n;
}

int vfs_ioctl(struct vfs_file *f, uint32_t request, uint32_t arg)
{
    if (!f || !f->dentry || !f->dentry->inode) return -1;
    return ioctl_fs(f->dentry->inode, request, arg);
}

void vfs_close(struct vfs_file *f)
{
    if (!f) return;
    if (f->refcount > 1) {
        f->refcount--;
        return;
    }
    if (f->dentry && f->dentry->inode) {
        close_fs(f->dentry->inode);
    }
    if (f->dentry) vfs_dentry_put(f->dentry);
    kfree(f);
}
