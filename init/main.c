#include "asm/multiboot.h"
#include "asm/page.h"
#include "asm/setup.h"
#include "linux/libc.h"
#include "linux/serial.h"
#include "linux/console.h"
#include "linux/init.h"
#include "linux/mm.h"
#include "linux/fs.h"
#include "linux/ramfs.h"
#include "linux/minixfs.h"
#include "linux/blkdev.h"
#include "linux/sysfs.h"
#include "linux/syscall.h"
#include "linux/timer.h"
#include "linux/time.h"
#include "linux/sched.h"
#include "linux/fork.h"
#include "linux/binfmts.h"
#include "linux/device.h"
#include "linux/params.h"
#include "linux/virtio_scsi.h"
#include "linux/printk.h"
#include "linux/tty.h"
#include "linux/version.h"
#include "linux/memlayout.h"
#include "linux/bootmem.h"
#include "linux/mmzone.h"
#include "linux/page_alloc.h"
#include "linux/vmscan.h"
#include "linux/swap.h"
#include "linux/slab.h"

extern initcall_t __initcall_start[];
extern initcall_t __initcall_end[];
struct multiboot_info boot_mbi;

/* do_initcalls: Perform initcalls. */
static void do_initcalls(void)
{
    initcall_t *call;
    for (call = __initcall_start; call < __initcall_end; call++) {
        (*call)();
    }
}

/* do_basic_setup: Perform basic setup. */
static void do_basic_setup(void)
{
    driver_init();
    do_initcalls();
    syscall_init();
    printf("Syscall handler installed.\n");
}

/* run_init_process: Run init process. */
static int run_init_process(const char *path)
{
    printf("kernel_init: exec %s as PID 1\n", path);
    return task_exec_user(path);
}

/* prepare_namespace: Mount the early filesystems and launch init. */
static void prepare_namespace(void)
{
    struct inode *inode;
    struct block_device *bdev;

    vfs_init();
    populate_rootfs();

    /*
     * Linux mapping: mountpoints are directories in the parent filesystem.
     * Create them on the root ramfs before mounting proc/devtmpfs/sysfs/minix.
     */
    (void)vfs_mkdir("/proc");
    (void)vfs_mkdir("/dev");
    (void)vfs_mkdir("/sys");
    (void)vfs_mkdir("/mnt");
    (void)vfs_mkdir("/mnt_nvme");

    if (sysfs_init() != 0)
        panic("sysfs init failed.");
    vfs_mount_fs("/proc", "proc");
    devtmpfs_mount("/dev");
    vfs_mount_fs("/sys", "sysfs");
    virtio_scsi_late_probe();
    /*
     * Linux mapping: mount explicit filesystems on explicit block devices.
     * Keep the legacy /mnt mount on the virtio-scsi disk when present so SCSI
     * and NVMe paths can be validated independently in one boot.
     */
    const char *scsi_minix_dev = "/dev/sda";
    const char *fallback_minix_dev = "/dev/ram1";
    if (vfs_resolve(scsi_minix_dev)) {
        inode = vfs_resolve(scsi_minix_dev);
        if (inode && (inode->flags & 0x7) == FS_BLOCKDEVICE) {
            bdev = (struct block_device *)inode->private_data;
            if (bdev && blkdev_get(bdev) == 0) {
                minix_prepare_example_image(bdev);
                blkdev_put(bdev);
            }
        }
        vfs_mount_fs_dev("/mnt", "minix", scsi_minix_dev);
    } else {
        inode = vfs_resolve(fallback_minix_dev);
        if (inode && (inode->flags & 0x7) == FS_BLOCKDEVICE) {
            bdev = (struct block_device *)inode->private_data;
            if (bdev && blkdev_get(bdev) == 0) {
                minix_prepare_example_image(bdev);
                blkdev_put(bdev);
            }
        }
        vfs_mount_fs_dev("/mnt", "minix", fallback_minix_dev);
    }

    if (vfs_resolve("/dev/nvme0n1")) {
        inode = vfs_resolve("/dev/nvme0n1");
        if (inode && (inode->flags & 0x7) == FS_BLOCKDEVICE) {
            bdev = (struct block_device *)inode->private_data;
            if (bdev && blkdev_get(bdev) == 0) {
                minix_prepare_example_image(bdev);
                blkdev_put(bdev);
            }
        }
        vfs_mount_fs_dev("/mnt_nvme", "minix", "/dev/nvme0n1");
    }

    const char *init = get_execute_command();
    if (run_init_process(init) != 0) {
        const char *fallbacks[] = {
            "/sbin/init",
            "/etc/init",
            "/bin/init",
            "/sbin/sh",
            "/bin/sh"
        };
        for (uint32_t i = 0; i < (sizeof(fallbacks) / sizeof(fallbacks[0])); i++) {
            if (!strcmp(init, fallbacks[i]))
                continue;
            if (run_init_process(fallbacks[i]) == 0)
                return;
        }
        panic("No init found. Try passing init= option to kernel.");
    }
}

/* kernel_init: Finish core setup and launch the first user-space init process. */
static void kernel_init(void)
{
    do_basic_setup();
    prepare_namespace();
}

/* rest_init: Create PID 1 and drop PID 0 into the idle loop. */
static void rest_init(void)
{
    /* 1. Create the kernel_init thread. It will become PID 1. */
    kernel_thread(kernel_init);

    /* 2. The current thread (PID 0) becomes the idle thread. */
    /* Enable Interrupts to start scheduling */
    __asm__ volatile ("sti");

    printf("rest_init: CPU entering idle loop (PID 0)\n");
    while (1)
        __asm__ volatile ("hlt");
}

/* start_kernel: Run the top-level kernel initialization sequence. */
void start_kernel(struct multiboot_info* mbi, uint32_t magic)
{
    /* Initialize serial port FIRST so we can debug without VGA */
    init_serial();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Invalid Multiboot magic number!");

    setup_arch(mbi);

    /* Initialize the entire Memory Management subsystem (PMM, VMM, KHEAP) */
    if (mbi->mods_addr == 0)
        panic("No Multiboot modules found! InitRD not loaded.");

    bootmem_init(mbi);
    printf("BOOTMEM initialized.\n");

    init_zones();
    printf("ZONES initialized.\n");

    build_all_zonelists();
    printf("ZONELISTS initialized.\n");

    free_area_init(mbi);
    printf("PAGE_ALLOC initialized.\n");

    paging_init();
    printf("PAGING initialized.\n");

    mem_init();
    printf("MEM initialized.\n");

    kswapd_init();
    printf("VMSCAN initialized.\n");

    swap_init();
    printf("SWAP initialized.\n");

    kmem_cache_init();
    printf("SLAB initialized.\n");

    sched_init();
    fork_init();

    boot_mbi = *mbi;
    if (boot_mbi.flags & 0x4)
        setup_command_line((const char *)memlayout_directmap_phys_to_virt(boot_mbi.cmdline));
    else
        setup_command_line(NULL);

    printk(linux_banner);

    /* Initialize PIT Timer (HZ = 10ms per tick) */
    init_timer(HZ);

    printf("Hello, Kernel World!\n");
    printf("This is a minimal kernel running on QEMU.\n");
    printf("Console output uses the serial port (COM1).\n");
    printf("Enjoy your OS development journey!\n");

    /* Transition to rest_init and spawn PID 1 */
    rest_init();
}
