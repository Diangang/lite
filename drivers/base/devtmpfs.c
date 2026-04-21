#include "linux/completion.h"
#include "linux/init.h"
#include "linux/fs.h"
#include "linux/file.h"
#include "linux/kthread.h"
#include "linux/libc.h"
#include "linux/spinlock.h"
#include "linux/device.h"
#include "linux/blkdev.h"
#include "linux/ramfs.h"
#include "base.h"

static struct dentry *devtmpfs_root;
static struct super_block *devtmpfs_sb;
static struct inode *devtmpfs_console_inode;
static struct inode *devtmpfs_tty_inode;
static struct task_struct *thread;
static struct completion setup_done;
static DEFINE_SPINLOCK(req_lock);
static struct file_system_type dev_fs_type;

struct req {
    struct req *next;
    struct completion done;
    const char *name;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    struct device *dev;
    int err;
};

static struct req *requests;

static const char *find_slash(const char *s)
{
    while (s && *s) {
        if (*s == '/')
            return s;
        s++;
    }
    return NULL;
}

static char *find_last_slash(char *s)
{
    char *last = NULL;

    while (s && *s) {
        if (*s == '/')
            last = s;
        s++;
    }
    return last;
}

static struct dentry *lookup_child(struct dentry *parent, const char *name)
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

static int create_path(const char *nodepath)
{
    char path[128];
    char *p;
    struct dentry *curr = devtmpfs_root;

    if (!devtmpfs_root || !nodepath || !nodepath[0])
        return -1;
    if (strlen(nodepath) >= sizeof(path))
        return -1;
    strcpy(path, nodepath);

    p = path;
    for (;;) {
        const char *slash = find_slash(p);
        struct dentry *child;
        if (!slash)
            return 0;
        if (slash == p) {
            p++;
            continue;
        }
        p[slash - p] = 0;
        child = lookup_child(curr, p);
        if (!child) {
            struct inode *inode = mkdir_fs(curr->inode, p);
            if (!inode)
                return -1;
            inode->i_private = &thread;
            child = d_alloc(curr, p);
            if (!child)
                return -1;
            child->inode = inode;
        }
        if (!child->inode || (child->inode->flags & 0x7) != FS_DIRECTORY)
            return -1;
        curr = child;
        p += (slash - p) + 1;
    }
}

static int handle_create(const char *nodename, uint32_t type, dev_t devt, void *private_data,
                         uint32_t mode, uint32_t uid, uint32_t gid, struct device *dev)
{
    struct inode *existing;
    uint32_t existing_type;

    existing = vfs_resolve_at(devtmpfs_root, nodename);
    if (existing) {
        if (!dev)
            return 0;
        existing_type = (dev->type == &disk_type) ? FS_BLOCKDEVICE : FS_CHARDEVICE;
        return special_inode_matches(existing, existing_type, dev->devt) ? 0 : -1;
    }
    if (vfs_mknod_at(devtmpfs_root, nodename, type, devt, private_data, mode, uid, gid) == 0)
        goto mark_owner;
    if (create_path(nodename) != 0)
        return -1;
    if (vfs_mknod_at(devtmpfs_root, nodename, type, devt, private_data, mode, uid, gid) != 0)
        return -1;

mark_owner:
    existing = vfs_resolve_at(devtmpfs_root, nodename);
    if (existing)
        existing->i_private = &thread;
    return 0;
}

static int dev_rmdir(const char *path)
{
    struct inode *inode = vfs_resolve_at(devtmpfs_root, path);

    if (!inode || inode->i_private != &thread)
        return -1;
    return vfs_rmdir_at(devtmpfs_root, path);
}

static int dev_mynode(struct device *dev, struct inode *inode)
{
    uint32_t type;

    if (!dev || !inode)
        return 0;
    if (inode->i_private != &thread)
        return 0;
    type = (dev->type == &disk_type) ? FS_BLOCKDEVICE : FS_CHARDEVICE;
    return special_inode_matches(inode, type, dev->devt);
}

static int delete_path(const char *nodepath)
{
    char path[128];

    if (!nodepath || !nodepath[0])
        return -1;
    if (strlen(nodepath) >= sizeof(path))
        return -1;
    strcpy(path, nodepath);

    for (;;) {
        char *base = find_last_slash(path);
        if (!base)
            break;
        *base = 0;
        if (!path[0])
            break;
        if (dev_rmdir(path) != 0)
            break;
    }
    return 0;
}

static int handle_remove(const char *nodename, struct device *dev)
{
    struct inode *existing = vfs_resolve_at(devtmpfs_root, nodename);

    if (!existing)
        return -1;
    if (!dev_mynode(dev, existing))
        return -1;
    if (vfs_unlink_at(devtmpfs_root, nodename) != 0)
        return -1;
    if (find_slash(nodename))
        delete_path(nodename);
    return 0;
}

static int handle(const char *name, uint32_t mode, uint32_t uid, uint32_t gid, struct device *dev)
{
    if (!devtmpfs_root)
        return -1;
    if (!name || !name[0])
        return -1;
    if (!mode)
        return handle_remove(name, dev);

    if (dev->type == &disk_type) {
        struct gendisk *disk = gendisk_from_dev(dev);
        struct block_device *bdev = disk ? bdget_disk(disk, 0) : NULL;
        int ret;
        if (!bdev)
            return -1;
        ret = handle_create(name, FS_BLOCKDEVICE, dev->devt, bdev, mode, uid, gid, dev);
        bdput(bdev);
        return ret;
    }

    if (!dev->devt)
        return -1;

    return handle_create(name, FS_CHARDEVICE, dev->devt, NULL, mode, uid, gid, dev);
}

static struct super_block *devtmpfs_get_sb(struct file_system_type *fs_type, int flags,
                                           const char *dev_name, void *data)
{
    if (!fs_type)
        return NULL;
    if (devtmpfs_sb) {
        devtmpfs_sb->refcount++;
        return devtmpfs_sb;
    }

    devtmpfs_sb = vfs_get_sb_single(fs_type, flags, dev_name, data);
    return devtmpfs_sb;
}

static int devtmpfsd(void *p)
{
    struct super_block *sb;
    int *err = (int *)p;

    if (!err)
        return -1;

    *err = 0;
    sb = devtmpfs_get_sb(&dev_fs_type, 0, NULL, NULL);
    if (!sb || !sb->s_root || !sb->s_root->inode) {
        *err = -1;
        complete(&setup_done);
        return *err;
    }
    complete(&setup_done);

    for (;;) {
        spin_lock(&req_lock);
        while (requests) {
            struct req *req = requests;

            requests = NULL;
            spin_unlock(&req_lock);
            while (req) {
                struct req *next = req->next;

                req->err = handle(req->name, req->mode, req->uid, req->gid, req->dev);
                complete(&req->done);
                req = next;
            }
            spin_lock(&req_lock);
        }
        spin_unlock(&req_lock);
        task_sleep(1);
    }

    return 0;
}

static int devtmpfs_submit_req(struct req *req)
{
    if (!thread || !req)
        return 0;

    init_completion(&req->done);
    req->err = 0;

    spin_lock(&req_lock);
    req->next = requests;
    requests = req;
    spin_unlock(&req_lock);

    wake_up_process(thread);
    wait_for_completion(&req->done);
    return req->err;
}

struct inode *devtmpfs_get_console(void)
{
    return devtmpfs_console_inode;
}

struct inode *devtmpfs_get_tty(void)
{
    return devtmpfs_tty_inode;
}

int devtmpfs_mount(const char *mntdir)
{
    if (!thread)
        return 0;
    if (!mntdir || !mntdir[0])
        return -1;
    return vfs_mount_fs(mntdir, "devtmpfs");
}

int devtmpfs_create_node(struct device *dev)
{
    struct req req;

    if (!thread)
        return 0;
    if (!dev)
        return -1;
    req.mode = 0;
    req.uid = 0;
    req.gid = 0;
    req.name = device_get_devnode(dev, &req.mode, &req.uid, &req.gid);
    /* Linux mapping: if the device has no devnode policy, devtmpfs is a no-op. */
    if (!req.name || !req.name[0])
        return 0;
    if (req.mode == 0)
        req.mode = 0600;
    req.dev = dev;
    return devtmpfs_submit_req(&req);
}

int devtmpfs_delete_node(struct device *dev)
{
    struct req req;

    if (!thread)
        return 0;
    if (!dev)
        return -1;
    req.name = device_get_devnode(dev, NULL, NULL, NULL);
    if (!req.name || !req.name[0])
        return 0;
    req.mode = 0;
    req.uid = 0;
    req.gid = 0;
    req.dev = dev;
    return devtmpfs_submit_req(&req);
}

static int devtmpfs_fill_super(struct super_block *sb, void *data, int silent)
{
    int ret = ramfs_fill_super(sb, data, silent);
    if (ret != 0)
        return ret;

    devtmpfs_root = sb->s_root;
    if (!devtmpfs_root || !devtmpfs_root->inode)
        return -1;

    (void)handle_create("tty", FS_CHARDEVICE, MKDEV(5, 0), NULL, 0666, 0, 0, NULL);
    (void)handle_create("console", FS_CHARDEVICE, MKDEV(5, 1), NULL, 0600, 0, 0, NULL);
    devtmpfs_tty_inode = vfs_resolve_at(devtmpfs_root, "tty");
    devtmpfs_console_inode = vfs_resolve_at(devtmpfs_root, "console");
    return 0;
}

static struct file_system_type dev_fs_type = {
    .name = "devtmpfs",
    .get_sb = devtmpfs_get_sb,
    .fill_super = devtmpfs_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

int devtmpfs_init(void)
{
    int err = register_filesystem(&dev_fs_type);

    if (err != 0)
        return err;
    init_completion(&setup_done);
    err = 0;
    thread = kthread_run(devtmpfsd, &err, "kdevtmpfs");
    if (!thread) {
        unregister_filesystem(&dev_fs_type);
        return -1;
    }
    wait_for_completion(&setup_done);
    if (err != 0) {
        thread = NULL;
        unregister_filesystem(&dev_fs_type);
        return err;
    }
    return 0;
}
