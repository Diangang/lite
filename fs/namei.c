#include "vfs.h"
#include "libc.h"
#include "task.h"
#include "ramfs.h"

int vfs_path_is_prefix(const char *path, const char *prefix, uint32_t *out_tail_off)
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

int vfs_build_abs(const char *path, char *abs, uint32_t cap)
{
    if (!path || !abs || cap < 2) return 0;
    if (path[0] == '/') return vfs_normalize_path(path, abs, cap);

    char tmp[256];
    uint32_t off = 0;
    const char *cwd = vfs_getcwd();
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

struct vfs_inode *vfs_resolve(const char *path)
{
    if (!path || !*path) return NULL;
    char abs[256];
    if (!vfs_build_abs(path, abs, sizeof(abs))) {
        return NULL;
    }
    uint32_t tail = 0;
    struct vfs_mount *m = vfs_find_mount(abs, &tail);
    if (!m || !m->sb || !m->sb->root) {
        return NULL;
    }
    const char *sub = abs + tail;
    while (*sub == '/') sub++;
    if (*sub == 0) {
        return m->sb->root;
    }

    if (m == &vfs_mounts[0] && m->sb->root == &vfs_root_node) {
        if (!vfs_base_root) return NULL;
        if (!*sub) return &vfs_root_node;
        struct vfs_inode *res = finddir_fs(vfs_base_root, (char*)sub);
        return res;
    }

    struct vfs_inode *node = finddir_fs(m->sb->root, (char*)sub);
    return node;
}
