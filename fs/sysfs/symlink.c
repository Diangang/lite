// symlink.c - Lite sysfs symlink support (Linux mapping: fs/sysfs/symlink.c)

#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/init.h"
#include "linux/time.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/kobject.h"
#include "linux/kobject.h"
#include "base.h"
#include "sysfs/sysfs.h"

static uint32_t sys_read_symlink(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);

struct file_operations sys_symlink_ops = {
    .read = sys_read_symlink,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};
static uint32_t sys_read_symlink(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || !node->private_data)
        return 0;
    const char *target = (const char *)node->private_data;
    uint32_t n = (uint32_t)strlen(target);
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, target + offset, size);
    return size;
}
static int sysfs_kobject_path(struct kobject *target, char *buf, uint32_t cap)
{
    if (!target || !buf || cap < 2)
        return -1;

    /*
     * Linux-like: sysfs paths come from the kobject parent chain rather than
     * from driver-core-specific type checks.
     *
     * Linux detail: when a kobject has no explicit parent, its effective parent
     * is the owning kset's kobject (see linux2.6/lib/kobject.c:kobject_add_internal()).
     * Lite doesn't always materialize that into kobj->parent, so use the same
     * effective-parent rule here to generate correct sysfs symlink targets.
     */
    uint32_t end = cap - 1;
    buf[end] = 0;

    struct kobject *k = target;
    while (k) {
        const char *name = k->name;
        if (!name || !name[0])
            return -1;
        uint32_t n = (uint32_t)strlen(name);
        if (n + 1 > end)
            return -1;
        end -= n;
        memcpy(buf + end, name, n);
        if (end == 0)
            return -1;
        buf[--end] = '/';
        k = sysfs_parent_kobj(k);
    }

    const char *prefix = "/sys";
    uint32_t pn = (uint32_t)strlen(prefix);
    if (pn > end)
        return -1;
    end -= pn;
    memcpy(buf + end, prefix, pn);

    /* Shift result to the front of buf. */
    memmove(buf, buf + end, cap - end);
    return 0;
}
static int sysfs_relpath(const char *from_dir, const char *to, char *out, uint32_t cap)
{
    if (!from_dir || !to || !out || cap < 2)
        return -1;
    if (from_dir[0] != '/' || to[0] != '/')
        return -1;

    /* Split both absolute paths into components without allocating. */
    const char *from_s[32];
    uint32_t from_n[32];
    const char *to_s[32];
    uint32_t to_n[32];
    uint32_t from_cnt = 0;
    uint32_t to_cnt = 0;

    const char *p = from_dir;
    while (*p == '/')
        p++;
    while (*p && from_cnt < 32) {
        const char *s = p;
        while (*p && *p != '/')
            p++;
        from_s[from_cnt] = s;
        from_n[from_cnt] = (uint32_t)(p - s);
        from_cnt++;
        while (*p == '/')
            p++;
    }

    p = to;
    while (*p == '/')
        p++;
    while (*p && to_cnt < 32) {
        const char *s = p;
        while (*p && *p != '/')
            p++;
        to_s[to_cnt] = s;
        to_n[to_cnt] = (uint32_t)(p - s);
        to_cnt++;
        while (*p == '/')
            p++;
    }

    /* Find common prefix (by path component). */
    uint32_t common = 0;
    while (common < from_cnt && common < to_cnt) {
        if (from_n[common] != to_n[common])
            break;
        int same = 1;
        for (uint32_t j = 0; j < from_n[common]; j++) {
            if (from_s[common][j] != to_s[common][j]) {
                same = 0;
                break;
            }
        }
        if (!same)
            break;
        common++;
    }

    uint32_t off = 0;
    out[0] = 0;

    /* Go up from from_dir to the common prefix. */
    for (uint32_t i = common; i < from_cnt; i++) {
        if (off + 3 >= cap)
            return -1;
        out[off++] = '.';
        out[off++] = '.';
        out[off++] = '/';
        out[off] = 0;
    }

    /* Append remaining to-path components. */
    for (uint32_t i = common; i < to_cnt; i++) {
        uint32_t n = to_n[i];
        if (n == 0)
            continue;
        if (off + n + 1 >= cap)
            return -1;
        memcpy(out + off, to_s[i], n);
        off += n;
        if (i + 1 < to_cnt)
            out[off++] = '/';
        out[off] = 0;
    }

    if (off == 0) {
        out[0] = '.';
        out[1] = 0;
    }

    return 0;
}
struct inode *sysfs_find_kobj_link_inode(struct kobject *kobj, const char *name)
{
    struct sysfs_dirent *cur = sysfs_find_named_dirent(kobj, name);
    if (!cur || cur->inode.f_ops == &sys_dead_ops || cur->inode.flags == 0)
        return NULL;
    return &cur->inode;
}
int sysfs_create_link(struct kobject *kobj, struct kobject *target, const char *name)
{
    char from_abs[256];
    char to_abs[256];
    char rel[256];
    if (!kobj || !target || !name || !name[0])
        return -1;
    if (sysfs_kobject_path(kobj, from_abs, sizeof(from_abs)) != 0)
        return -1;
    if (sysfs_kobject_path(target, to_abs, sizeof(to_abs)) != 0)
        return -1;

    /*
     * Linux mapping: sysfs stores relative symlink targets (e.g. class device
     * "device" link points to "../../devices/...").
     *
     * Lite path walker already resolves relative symlink targets via dentry
     * paths + normalization, so prefer relative targets here.
     */
    const char *target_str = to_abs;
    if (sysfs_relpath(from_abs, to_abs, rel, sizeof(rel)) == 0)
        target_str = rel;

    struct inode *inode = sysfs_set_kobj_link_inode(kobj, name, target_str);
    if (!inode)
        return -1;
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (sd && sd->dentry)
        sysfs_dentry_refresh_child(sd->dentry, name, inode);
    return 0;
}
void sysfs_remove_link(struct kobject *kobj, const char *name)
{
    (void)sysfs_set_kobj_link_inode(kobj, name, NULL);
    struct sysfs_dirent *sd = sysfs_get_kobj_sd(kobj);
    if (sd && sd->dentry)
        sysfs_dentry_detach_child(sd->dentry, name);
}
