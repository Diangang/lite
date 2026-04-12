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
#include "linux/procfs.h"
#include "linux/devtmpfs.h"
#include "linux/minixfs.h"
#include "linux/sysfs.h"
#include "linux/sysfs.h"
#include "linux/syscall.h"
#include "linux/timer.h"
#include "linux/time.h"
#include "linux/sched.h"
#include "linux/fork.h"
#include "linux/binfmts.h"
#include "linux/device.h"
#include "linux/params.h"
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
    for (call = __initcall_start; call < __initcall_end; call++)
        (*call)();
}

/* do_basic_setup: Perform basic setup. */
static void do_basic_setup(void)
{
    driver_init();
    do_initcalls();
    init_syscall();
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
    vfs_init();
    populate_rootfs();

    if (sysfs_init() != 0)
        panic("sysfs init failed.");
    vfs_mount_fs("/proc", "proc");
    vfs_mount_fs("/dev", "devtmpfs");
    sysfs_mount();
    /*
     * Linux-style: mount a filesystem on a block device node.
     * Prefer NVMe when present (QEMU -device nvme), otherwise fall back to ramdisk.
     */
    const char *minix_dev = "/dev/ram1";
    if (vfs_resolve("/dev/nvme0n1"))
        minix_dev = "/dev/nvme0n1";
    if (!strcmp(minix_dev, "/dev/ram1")) {
        struct inode *ram1 = vfs_resolve(minix_dev);
        if (ram1 && (ram1->flags & 0x7) == FS_BLOCKDEVICE)
            minix_seed_example_image((struct block_device *)ram1->private_data);
    }
    vfs_mount_fs_dev("/mnt", "minix", minix_dev);

    const char *init = get_init_process();
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
