#include "vfs.h"
#include "kheap.h"
#include "libc.h"

static vfs_mount_t vfs_mounts[8];
static uint32_t vfs_mount_count = 0;
static fs_node_t vfs_root_node;
static fs_node_t *vfs_base_root = NULL;
static struct dirent vfs_root_dirent;
static char vfs_cwd[128];

void vfs_init(void)
{
    memset(vfs_mounts, 0, sizeof(vfs_mounts));
    vfs_mount_count = 0;
    memset(&vfs_root_node, 0, sizeof(vfs_root_node));
    strcpy(vfs_root_node.name, "/");
    vfs_root_node.flags = FS_DIRECTORY;
    vfs_root_node.inode = 0xEEEE0001;
    vfs_root_node.readdir = NULL;
    vfs_root_node.finddir = NULL;
    vfs_base_root = NULL;
    strcpy(vfs_cwd, "/");
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
    uint32_t cwd_len = (uint32_t)strlen(vfs_cwd);
    if (cwd_len == 0) strcpy(vfs_cwd, "/");
    if (cwd_len >= sizeof(tmp) - 1) return 0;
    memcpy(tmp, vfs_cwd, cwd_len);
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
    return vfs_cwd;
}

int vfs_chdir(const char *path)
{
    if (!path || !*path) return -1;
    char abs[256];
    if (!vfs_build_abs(path, abs, sizeof(abs))) return -1;
    fs_node_t *node = vfs_resolve(abs);
    if (!node) return -1;
    if ((node->flags & 0x7) != FS_DIRECTORY) return -1;
    uint32_t n = (uint32_t)strlen(abs);
    if (n >= sizeof(vfs_cwd)) n = sizeof(vfs_cwd) - 1;
    memcpy(vfs_cwd, abs, n);
    vfs_cwd[n] = 0;
    return 0;
}

static struct dirent *vfs_root_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
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
    return readdir_fs(vfs_base_root, index - n_mounts);
}

static fs_node_t *vfs_root_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
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

int vfs_mount_root(const char *path, fs_node_t *root_node)
{
    if (!path || !root_node) return -1;
    if (vfs_mount_count >= (sizeof(vfs_mounts) / sizeof(vfs_mounts[0]))) return -1;

    vfs_mount_t *m = &vfs_mounts[vfs_mount_count++];
    m->path = path;
    m->sb = (vfs_super_block_t*)kmalloc(sizeof(vfs_super_block_t));
    if (!m->sb) return -1;
    m->sb->name = path;
    if (path[0] == '/' && path[1] == 0) {
        vfs_base_root = root_node;
        vfs_root_node.readdir = &vfs_root_readdir;
        vfs_root_node.finddir = &vfs_root_finddir;
        m->sb->root = &vfs_root_node;
    } else {
        m->sb->root = root_node;
    }
    m->sb->fs_private = NULL;
    return 0;
}

static vfs_mount_t *vfs_find_mount(const char *path, uint32_t *out_tail_off)
{
    vfs_mount_t *best = NULL;
    uint32_t best_len = 0;
    uint32_t tail = 0;

    for (uint32_t i = 0; i < vfs_mount_count; i++) {
        vfs_mount_t *m = &vfs_mounts[i];
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

fs_node_t *vfs_resolve(const char *path)
{
    if (!path || !*path) return NULL;
    char abs[256];
    if (!vfs_build_abs(path, abs, sizeof(abs))) return NULL;
    uint32_t tail = 0;
    vfs_mount_t *m = vfs_find_mount(abs, &tail);
    if (!m || !m->sb || !m->sb->root) return NULL;
    const char *sub = abs + tail;
    if (*sub == 0) return m->sb->root;
    return finddir_fs(m->sb->root, (char*)sub);
}

vfs_file_t *vfs_open(const char *path, uint32_t flags)
{
    fs_node_t *node = vfs_resolve(path);
    if (!node) return NULL;

    return vfs_open_node(node, flags);
}

vfs_file_t *vfs_open_node(fs_node_t *node, uint32_t flags)
{
    if (!node) return NULL;
    vfs_inode_t *ino = (vfs_inode_t*)kmalloc(sizeof(vfs_inode_t));
    vfs_dentry_t *d = (vfs_dentry_t*)kmalloc(sizeof(vfs_dentry_t));
    vfs_file_t *f = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!ino || !d || !f) {
        if (ino) kfree(ino);
        if (d) kfree(d);
        if (f) kfree(f);
        return NULL;
    }

    ino->node = node;
    ino->mode = node->flags;
    d->name = node->name;
    d->parent = NULL;
    d->inode = ino;
    f->dentry = d;
    f->pos = 0;
    f->flags = flags;
    open_fs(node, 1, 1);
    return f;
}

uint32_t vfs_read(vfs_file_t *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode || !f->dentry->inode->node) return 0;
    uint32_t n = read_fs(f->dentry->inode->node, f->pos, len, buf);
    f->pos += n;
    return n;
}

uint32_t vfs_write(vfs_file_t *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode || !f->dentry->inode->node) return 0;
    uint32_t n = write_fs(f->dentry->inode->node, f->pos, len, (uint8_t*)buf);
    f->pos += n;
    return n;
}

void vfs_close(vfs_file_t *f)
{
    if (!f) return;
    if (f->dentry && f->dentry->inode && f->dentry->inode->node) {
        close_fs(f->dentry->inode->node);
    }
    if (f->dentry) {
        if (f->dentry->inode) kfree(f->dentry->inode);
        kfree(f->dentry);
    }
    kfree(f);
}
