#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/fs.h"
#include "linux/file.h"
#include "linux/init.h"
#include "linux/sched.h"
#include "linux/fdtable.h"
/* fs/proc internal APIs (Linux mapping: fs/proc/internal.h) */
#include "internal.h"
#include "linux/time.h"
#include "linux/interrupt.h"
#include "linux/slab.h"
#include "linux/gfp.h"
#include "linux/mmzone.h"
#include "linux/mmzone.h"
#include "linux/pagemap.h"
#include "linux/blkdev.h"
#include "linux/buffer_head.h"
#include "linux/device.h"
#include "base.h"
#include "linux/bootmem.h"
#include "asm/pgtable.h"
#include "linux/kernel.h"
#include "asm/pgtable.h"

static struct dirent proc_dirent;
// proc_root is allocated when procfs superblock is built.
static struct inode_operations procfs_dir_iops;

/*
 * Minimal proc_dir_entry-style model (Linux mapping):
 * - Linux procfs uses proc_dir_entry objects to build a dynamic tree.
 * - Lite keeps the implementation small; use a fixed root child table and
 */
struct proc_dir_entry {
    const char *name;
    struct inode *inode;
};

static struct proc_dir_entry proc_root_children[16];
static uint32_t proc_root_children_nr;

void proc_register_root_child(const char *name, struct inode *inode)
{
    if (!name || !name[0] || !inode)
        return;
    if (proc_root_children_nr >= (uint32_t)(sizeof(proc_root_children) / sizeof(proc_root_children[0])))
        return;
    proc_root_children[proc_root_children_nr].name = name;
    proc_root_children[proc_root_children_nr].inode = inode;
    proc_root_children_nr++;
}

static struct inode *proc_lookup_root_child(const char *name)
{
    for (uint32_t i = 0; i < proc_root_children_nr; i++) {
        if (!proc_root_children[i].name)
            continue;
        if (!strcmp(proc_root_children[i].name, name))
            return proc_root_children[i].inode;
    }
    return NULL;
}

static struct dirent *proc_fill_dirent(const char *name, uint32_t ino)
{
    if (!name || !name[0])
        return NULL;
    strcpy(proc_dirent.name, name);
    proc_dirent.ino = ino;
    return &proc_dirent;
}

/* proc_readdir: Implement proc readdir. */
static struct dirent *proc_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index < proc_root_children_nr) {
        const char *name = proc_root_children[index].name;
        struct inode *inode = proc_root_children[index].inode;
        return proc_fill_dirent(name, inode ? inode->i_ino : 0);
    }

    /* After fixed children, list bounded per-pid entries if present. */
    struct dirent *dent = proc_pid_readdir_root(index - proc_root_children_nr);
    if (dent)
        return dent;

    return NULL;
}

/* proc_finddir: Implement proc finddir. */
static struct inode *proc_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct inode *fixed = proc_lookup_root_child(name);
    if (fixed)
        return fixed;
    return proc_pid_lookup(name);
}

static struct file_operations procfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = proc_readdir,
    .ioctl = NULL
};

static struct inode_operations procfs_dir_iops = {
    .lookup = proc_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

/* proc_fill_super: Implement proc fill super. */
static int proc_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    proc_pid_init();
    proc_root_children_nr = 0;

    struct inode *proc_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!proc_root)
        return -1;

    memset(proc_root, 0, sizeof(struct inode));
    proc_root->flags = FS_DIRECTORY;
    proc_root->i_ino = 1;
    proc_root->i_op = &procfs_dir_iops;
    proc_root->f_ops = &procfs_dir_ops;
    proc_root->uid = 0;
    proc_root->gid = 0;
    proc_root->i_mode = 0555;

    proc_interrupts_init();

    proc_meminfo_init();
    proc_generic_init();
    /* Keep self as a special dynamic entry. */
    proc_register_root_child("self", proc_get_pid_dir(0xFFFFFFFF));

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = proc_root;

    return 0;
}

static struct file_system_type proc_fs_type = {
    .name = "proc",
    .get_sb = vfs_get_sb_single,
    .fill_super = proc_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* proc_root_init: Register the proc filesystem. */
static int proc_root_init(void)
{
    register_filesystem(&proc_fs_type);
    printf("proc filesystem registered.\n");
    return 0;
}
fs_initcall(proc_root_init);
