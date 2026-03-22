#include "vfs.h"
#include "libc.h"
#include "initrd.h"
#include "ramfs.h"
#include "procfs.h"
#include "devfs.h"
#include "sysfs.h"
#include "device_model.h"
#include "console.h"
#include "multiboot.h"

static void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv) return;
    memset(drv, 0, sizeof(*drv));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(drv->kobj.name)) n = sizeof(drv->kobj.name) - 1;
        memcpy(drv->kobj.name, name, n);
        drv->kobj.name[n] = 0;
    }
    drv->kobj.refcount = 1;
    drv->bus = bus;
    drv->probe = probe;
}

static int probe_nop(struct device *dev)
{
    (void)dev;
    return 0;
}

void init_fs(struct multiboot_info* mbi)
{
    /* Create the Root Filesystem (ramfs) first */
    struct vfs_inode *ram_root = ramfs_init();
    vfs_init();
    vfs_mount_root("/", ram_root);
    printf("Root filesystem (ramfs) mounted.\n");

    /* Load and mount the initrd image */
    struct vfs_inode *initrd_root = init_initrd(mbi);
    if (!initrd_root)
        panic("Failed to load Ramdisk.");
    vfs_mount_root("/initrd", initrd_root);
    printf("Ramdisk loaded and mounted to /initrd.\n");

    /* Initialize other filesystems */
    struct vfs_inode *proc_root = procfs_init();
    struct vfs_inode *dev_root = devfs_init();
    
    device_model_init();
    struct bus_type *platform = device_model_platform_bus();
    if (platform) {
        device_register_simple("console", "console", platform, devfs_get_console());
        device_register_simple("initrd", "initrd", platform, initrd_root);
    }
    
    struct vfs_inode *sys_root = sysfs_init();
    
    if (platform) {
        device_register_simple("ramfs", "memfs", platform, ram_root);
        static struct device_driver drv_console;
        static struct device_driver drv_initrd;
        static struct device_driver drv_memfs;
        init_driver(&drv_console, "console", platform, probe_nop);
        init_driver(&drv_initrd, "initrd", platform, probe_nop);
        init_driver(&drv_memfs, "memfs", platform, probe_nop);
        driver_register(&drv_console);
        driver_register(&drv_initrd);
        driver_register(&drv_memfs);
    }
    
    /* Mount pseudo-filesystems */
    vfs_mount_root("/proc", proc_root);
    vfs_mount_root("/dev", dev_root);
    vfs_mount_root("/sys", sys_root);
    
    // Set cwd to root
    vfs_chdir("/");
}

uint32_t read_fs(struct vfs_inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (node->f_ops && node->f_ops->read != NULL)
        return node->f_ops->read(node, offset, size, buffer);
    return 0;
}

uint32_t write_fs(struct vfs_inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (node->f_ops && node->f_ops->write != NULL)
        return node->f_ops->write(node, offset, size, buffer);
    return 0;
}

void open_fs(struct vfs_inode *node, uint8_t read, uint8_t write)
{
    (void)read;
    (void)write;
    if (node->f_ops && node->f_ops->open != NULL)
        node->f_ops->open(node);
}

void close_fs(struct vfs_inode *node)
{
    if (node->f_ops && node->f_ops->close != NULL)
        node->f_ops->close(node);
}

struct dirent *readdir_fs(struct vfs_inode *node, uint32_t index)
{
    if ((node->flags & 0x7) == FS_DIRECTORY && node->f_ops && node->f_ops->readdir != NULL)
        return node->f_ops->readdir(node, index);
    return NULL;
}

struct vfs_inode *finddir_fs(struct vfs_inode *node, const char *name)
{
    if (!node || !name) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY || !node->f_ops || node->f_ops->finddir == NULL) return NULL;

    while (*name == '/') name++;
    if (*name == 0) return node;

    const char *slash = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '/') {
            slash = p;
            break;
        }
    }
    
    if (!slash) {
        struct vfs_inode *res = node->f_ops->finddir(node, name);
        // printf("DEBUG: calling finddir on '%s' -> %p\n", name, res);
        return res;
    }

    char part[128];
    uint32_t n = (uint32_t)(slash - name);
    if (n == 0 || n >= sizeof(part)) return NULL;
    memcpy(part, name, n);
    part[n] = 0;

    struct vfs_inode *child = node->f_ops->finddir(node, part);
    if (!child) return NULL;
    
    // Quick debug point
    // printf("DEBUG: recursive finddir_fs part '%s' found!\n", part);

    while (*slash == '/') slash++;
    if (*slash == 0) return child;
    return finddir_fs(child, slash);
}

int ioctl_fs(struct vfs_inode *node, uint32_t request, uint32_t arg)
{
    if (!node || !node->f_ops || node->f_ops->ioctl == NULL) return -1;
    return node->f_ops->ioctl(node, request, arg);
}
