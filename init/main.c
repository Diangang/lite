#include "asm/setup.h"
#include "asm/page.h"
#include "asm/setup.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/serial.h"
#include "linux/console.h"
#include "linux/init.h"
#include "linux/mm.h"
#include "linux/fs.h"
#include "linux/ramfs.h"
#include "minix/minix.h"
#include "linux/blkdev.h"
#include "linux/sysfs.h"
#include "linux/syscalls.h"
#include "linux/timer.h"
#include "linux/time.h"
#include "linux/sched.h"
#include "linux/sched.h"
#include "linux/binfmts.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/virtio_scsi.h"
#include "linux/printk.h"
#include "linux/tty.h"
#include "linux/printk.h"
#include "linux/start_kernel.h"
#include "asm/pgtable.h"
#include "linux/bootmem.h"
#include "linux/mmzone.h"
#include "linux/gfp.h"
#include "linux/mmzone.h"
#include "linux/swap.h"
#include "linux/slab.h"

/*
 * Linux mapping: linux2.6/init/main.c boot command line storage.
 *
 * Lite keeps fixed-size buffers (subsetting Linux), but uses Linux symbol names
 * and placement: saved_command_line + execute_command live in init/main.c.
 */
char boot_command_line[COMMAND_LINE_SIZE] __initdata;
static char saved_command_line_buf[256];
char *saved_command_line = saved_command_line_buf;
static char initcall_command_line_buf[sizeof(saved_command_line_buf)];
static char *initcall_command_line = initcall_command_line_buf;

static char execute_command_buf[64];
static char *execute_command;

static void set_execute_command(const char *value, size_t len)
{
    if (len >= sizeof(execute_command_buf))
        len = sizeof(execute_command_buf) - 1;
    if (len)
        memcpy(execute_command_buf, value, len);
    execute_command_buf[len] = '\0';
    execute_command = execute_command_buf;
}

static void parse_command_line(void)
{
    size_t i = 0;

    execute_command = NULL;
    execute_command_buf[0] = '\0';

    while (saved_command_line && saved_command_line[i]) {
        while (saved_command_line[i] == ' ')
            i++;
        if (!saved_command_line[i])
            break;
        if (!strncmp(&saved_command_line[i], "init=", 5)) {
            size_t start = i + 5;
            size_t end = start;
            while (saved_command_line[end] && saved_command_line[end] != ' ')
                end++;
            set_execute_command(&saved_command_line[start], end - start);
            return;
        }
        while (saved_command_line[i] && saved_command_line[i] != ' ')
            i++;
    }
}

void setup_command_line(const char *cmdline)
{
    size_t boot_len = 0;
    size_t len = 0;

    if (cmdline)
        boot_len = strlen(cmdline);

    if (boot_len >= sizeof(boot_command_line))
        boot_len = sizeof(boot_command_line) - 1;

    if (boot_len)
        memcpy(boot_command_line, cmdline, boot_len);

    boot_command_line[boot_len] = '\0';
    len = boot_len;

    if (len >= sizeof(saved_command_line_buf))
        len = sizeof(saved_command_line_buf) - 1;

    if (len)
        memcpy(saved_command_line_buf, boot_command_line, len);

    saved_command_line_buf[len] = '\0';
    strcpy(initcall_command_line, saved_command_line);
    parse_command_line();
}

const char *get_execute_command(void)
{
    return execute_command;
}

int get_cmdline_param(const char *key, char *value, size_t cap)
{
    if (!key || !key[0] || !value || cap == 0)
        return -1;

    value[0] = '\0';

    size_t key_len = strlen(key);
    size_t i = 0;
    while (saved_command_line && saved_command_line[i]) {
        while (saved_command_line[i] == ' ')
            i++;
        if (!saved_command_line[i])
            break;

        size_t start = i;
        while (saved_command_line[i] && saved_command_line[i] != ' ')
            i++;
        size_t end = i;

        if (end > start + key_len &&
            !strncmp(&saved_command_line[start], key, key_len) &&
            saved_command_line[start + key_len] == '=') {
            size_t vstart = start + key_len + 1;
            size_t vlen = end - vstart;
            if (vlen >= cap)
                vlen = cap - 1;
            if (vlen)
                memcpy(value, &saved_command_line[vstart], vlen);
            value[vlen] = '\0';
            return 0;
        }
        if (end == start + key_len &&
            !strncmp(&saved_command_line[start], key, key_len)) {
            value[0] = '\0';
            return 0;
        }
    }
    return -1;
}

extern initcall_t __initcall_start[];
extern initcall_t __initcall0_start[];
extern initcall_t __initcall1_start[];
extern initcall_t __initcall2_start[];
extern initcall_t __initcall3_start[];
extern initcall_t __initcall4_start[];
extern initcall_t __initcall5_start[];
extern initcall_t __initcall6_start[];
extern initcall_t __initcall7_start[];
extern initcall_t __initcall_end[];

static initcall_t *initcall_levels[] __initdata = {
    __initcall0_start,
    __initcall1_start,
    __initcall2_start,
    __initcall3_start,
    __initcall4_start,
    __initcall5_start,
    __initcall6_start,
    __initcall7_start,
    __initcall_end
};

/* Keep these in sync with initcalls in include/linux/init.h */
static char *initcall_level_names[] __initdata = {
    "early",
    "core",
    "postcore",
    "arch",
    "subsys",
    "fs",
    "device",
    "late"
};

int do_one_initcall(initcall_t fn)
{
    if (!fn)
        return -1;
    return fn();
}

/* do_pre_smp_initcalls: Lite subset runs early initcalls before driver_init. */
static void __init do_pre_smp_initcalls(void)
{
    initcall_t *call;
    for (call = __initcall_start; call < __initcall0_start; call++)
        (void)do_one_initcall(*call);
}

static void __init do_initcall_level(int level)
{
    initcall_t *call;
    if (level < 0 ||
        level >= (int)(sizeof(initcall_level_names) / sizeof(initcall_level_names[0])) ||
        level >= (int)(sizeof(initcall_levels) / sizeof(initcall_levels[0])) - 1)
        return;

    strcpy(initcall_command_line, saved_command_line);
    for (call = initcall_levels[level]; call < initcall_levels[level + 1]; call++)
        (void)do_one_initcall(*call);
}

/* do_initcalls: Perform initcalls by Linux-shaped level (0..7). */
static void __init do_initcalls(void)
{
    for (int level = 0; level < (int)(sizeof(initcall_levels) / sizeof(initcall_levels[0])) - 1; level++)
        do_initcall_level(level);
}

/* do_basic_setup: Perform basic setup. */
static void __init do_basic_setup(void)
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
    if (init) {
        if (run_init_process(init) == 0)
            return;
        panic("Requested init failed.");
    }

    const char *fallbacks[] = {
        "/sbin/init",
        "/etc/init",
        "/bin/init",
        "/sbin/sh",
        "/bin/sh"
    };
    for (uint32_t i = 0; i < (sizeof(fallbacks) / sizeof(fallbacks[0])); i++) {
        if (run_init_process(fallbacks[i]) == 0)
            return;
    }
    panic("No init found. Try passing init= option to kernel.");
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
void start_kernel(void)
{
    /* Initialize serial port FIRST so we can debug without VGA */
    init_serial();
    setup_arch(&boot_mbi);

    /* Initialize the entire Memory Management subsystem (PMM, VMM, KHEAP) */
    if (boot_mbi.mods_addr == 0)
        panic("No Multiboot modules found! InitRD not loaded.");

    bootmem_init(&boot_mbi);
    printf("BOOTMEM initialized.\n");

    init_zones();
    printf("ZONES initialized.\n");

    build_all_zonelists();
    printf("ZONELISTS initialized.\n");

    free_area_init(&boot_mbi);
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

    /*
     * Linux mapping: run early initcalls before starting PID 1 (kernel_init).
     * Lite is single-core, but we keep the phase boundary aligned.
     */
    do_pre_smp_initcalls();

    /* Transition to rest_init and spawn PID 1 */
    rest_init();
}
