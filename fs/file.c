#include "linux/file.h"
#include "linux/namei.h"
#include "linux/slab.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/fdtable.h"
#include "linux/fs.h"
#include "linux/ramfs.h"
#include "linux/pagemap.h"

/* vfs_open: Implement vfs open. */
struct file *vfs_open(const char *path, uint32_t flags)
{
    struct path lookup;
    struct dentry *dentry = NULL;
    struct inode *node = dentry ? dentry->inode : NULL;

    if (kern_path(path, 0, &lookup) == 0) {
        dentry = lookup.dentry;
        node = dentry ? dentry->inode : NULL;
    }

    if ((!dentry || !node) && (flags & VFS_O_CREAT)) {
        struct path parent;
        char name[128];
        int last_type;

        if (filename_parentat(AT_FDCWD, path, 0, &parent, name, sizeof(name), &last_type) != 0)
            return NULL;
        if (last_type != LAST_NORM)
            return NULL;
        if (!parent.dentry || !parent.dentry->inode ||
            (parent.dentry->inode->flags & 0x7) != FS_DIRECTORY)
            return NULL;

        struct inode *created = create_fs(parent.dentry->inode, name);
        if (!created)
            return NULL;

        struct dentry *new_d = d_lookup(parent.dentry, name);
        if (new_d) {
            new_d->inode = created;
            new_d->d_flags &= ~DENTRY_NEGATIVE;
            dentry = new_d;
        } else {
            new_d = d_alloc(parent.dentry, name);
            if (!new_d)
                return NULL;
            new_d->inode = created;
            dentry = new_d;
        }
        node = created;
    }

    if (!dentry || !node)
        return NULL;

    if ((node->flags & 0x7) == FS_DIRECTORY) {
        if (!vfs_check_access(node, 1, 0, 1))
        return NULL;
    } else {
        int need_write = (flags & VFS_O_TRUNC) ? 1 : 0;
        if (!vfs_check_access(node, 1, need_write, 0))
        return NULL;
    }

    struct file *f = vfs_open_dentry(dentry, flags);
    if (!f)
        return NULL;
    if ((flags & VFS_O_TRUNC) && (node->flags & 0x7) == FS_FILE) {
        node->i_size = 0;
        if (node->i_mapping)
            truncate_inode_pages(node->i_mapping, 0);
    }
    return f;
}

/* vfs_open_dentry: Implement vfs open dentry. */
struct file *vfs_open_dentry(struct dentry *dentry, uint32_t flags)
{
    if (!dentry || !dentry->inode)
        return NULL;
    struct file *f = (struct file*)kmalloc(sizeof(struct file));
    if (!f)
        return NULL;
    dentry->refcount++;
    f->dentry = dentry;
    f->pos = 0;
    f->flags = flags;
    f->refcount = 1;
    open_fs(dentry->inode, 1, 1);
    return f;
}

/* vfs_open_node: Implement vfs open node. */
struct file *vfs_open_node(struct inode *node, uint32_t flags)
{
    if (!node)
        return NULL;
    struct dentry *d = vfs_dentry_get(node, "");
    if (!d)
        return NULL;
    struct file *f = vfs_open_dentry(d, flags);
    vfs_dentry_put(d);
    return f;
}

/* vfs_read: Implement vfs read. */
uint32_t vfs_read(struct file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode)
        return 0;
    if (!vfs_check_access(f->dentry->inode, 1, 0, 0))
        return 0;
    uint32_t n = read_fs(f->dentry->inode, f->pos, len, buf);
    f->pos += n;
    return n;
}

/* vfs_write: Implement vfs write. */
uint32_t vfs_write(struct file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode)
        return 0;
    if (!vfs_check_access(f->dentry->inode, 0, 1, 0))
        return 0;
    uint32_t n = write_fs(f->dentry->inode, f->pos, len, (uint8_t*)buf);
    f->pos += n;
    return n;
}

/* vfs_ioctl: Implement vfs ioctl. */
int vfs_ioctl(struct file *f, uint32_t request, uint32_t arg)
{
    if (!f || !f->dentry || !f->dentry->inode)
        return -1;
    return ioctl_fs(f->dentry->inode, request, arg);
}

/* vfs_close: Implement vfs close. */
void vfs_close(struct file *f)
{
    if (!f)
        return;
    if (f->refcount > 1) {
        f->refcount--;
        return;
    }
    if (f->dentry && f->dentry->inode)
        close_fs(f->dentry->inode);
    if (f->dentry) vfs_dentry_put(f->dentry);
    kfree(f);
}

/* file_open_node: Implement file open node. */
struct file *file_open_node(struct inode *node, uint32_t flags)
{
    if (!node)
        return NULL;
    return vfs_open_node(node, flags);
}

/* file_open_path: Implement file open path. */
struct file *file_open_path(const char *path, uint32_t flags)
{
    if (!path)
        return NULL;
    return vfs_open(path, flags);
}

/* file_read: Implement file read. */
uint32_t file_read(struct file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !buf || len == 0)
        return 0;
    return vfs_read(f, buf, len);
}

/* file_write: Implement file write. */
uint32_t file_write(struct file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !buf || len == 0)
        return 0;
    return vfs_write(f, buf, len);
}

/* file_ioctl: Implement file ioctl. */
int file_ioctl(struct file *f, uint32_t request, uint32_t arg)
{
    if (!f)
        return -1;
    return vfs_ioctl(f, request, arg);
}

/* file_dup: Implement file dup. */
struct file *file_dup(struct file *f)
{
    if (!f)
        return NULL;
    f->refcount++;
    return f;
}

/* file_close: Implement file close. */
void file_close(struct file *f)
{
    if (!f)
        return;
    vfs_close(f);
}

/*
 * Linux mapping: fdtable management is part of fs/file.c (no standalone
 * fs/fdtable.c in linux2.6/). Lite keeps a fixed-size per-task fd table.
 */

void files_init(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        task->files.fdt.used[i] = 0;
        task->files.fdt.fd[i] = NULL;
    }
}

void files_close_all(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (task->files.fdt.used[i]) {
            if (task->files.fdt.fd[i])
                file_close(task->files.fdt.fd[i]);
            task->files.fdt.used[i] = 0;
            task->files.fdt.fd[i] = NULL;
        }
    }
}

void files_clone(struct task_struct *dst, struct task_struct *src)
{
    if (!dst || !src)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (src->files.fdt.used[i] && src->files.fdt.fd[i]) {
            dst->files.fdt.used[i] = 1;
            dst->files.fdt.fd[i] = file_dup(src->files.fdt.fd[i]);
        }
    }
}

int get_unused_fd(struct file *file)
{
    if (!current || !file)
        return -1;
    for (int i = 3; i < TASK_FD_MAX; i++) {
        if (!current->files.fdt.used[i]) {
            current->files.fdt.used[i] = 1;
            current->files.fdt.fd[i] = file;
            return i;
        }
    }
    return -1;
}

struct file *fget(int fd)
{
    if (!current)
        return NULL;
    if (fd < 0 || fd >= TASK_FD_MAX)
        return NULL;
    if (!current->files.fdt.used[fd])
        return NULL;
    if (!current->files.fdt.fd[fd])
        return NULL;
    return current->files.fdt.fd[fd];
}

int close_fd(int fd)
{
    if (fd < 3)
        return -1;
    struct file *f = fget(fd);
    if (!f)
        return -1;
    file_close(f);
    current->files.fdt.used[fd] = 0;
    current->files.fdt.fd[fd] = NULL;
    return 0;
}

void install_stdio(struct inode *console)
{
    if (!current || !console)
        return;

    current->files.fdt.used[0] = 1;
    current->files.fdt.fd[0] = file_open_node(console, 0);

    current->files.fdt.used[1] = 1;
    current->files.fdt.fd[1] = file_open_node(console, 0);

    current->files.fdt.used[2] = 1;
    current->files.fdt.fd[2] = file_open_node(console, 0);
}
