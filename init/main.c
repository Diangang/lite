#include "asm/multiboot.h"
#include "asm/page.h"
#include "asm/setup.h"
#include "linux/libc.h"
#include "linux/serial.h"
#include "linux/vga.h"
#include "linux/console.h"
#include "linux/init.h"
#include "linux/mm.h"
#include "linux/fs.h"
#include "linux/ramfs.h"
#include "linux/procfs.h"
#include "linux/devtmpfs.h"
#include "linux/sysfs.h"
#include "linux/ksysfs.h"
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

extern initcall_t __initcall_start[];
extern initcall_t __initcall_end[];
struct multiboot_info boot_mbi;

static void do_initcalls(void)
{
    initcall_t *call;
    for (call = __initcall_start; call < __initcall_end; call++)
        (*call)();
}

void populate_rootfs(void);

static void do_basic_setup(void)
{
    driver_init();
    do_initcalls();
    init_syscall();
    printf("Syscall handler installed.\n");
}

static int run_init_process(const char *path)
{
    printf("kernel_init: exec %s as PID 1\n", path);
    return task_exec_user(path);
}

static void prepare_namespace(void)
{
    vfs_init();
    populate_rootfs();

    vfs_mount_fs("/proc", "proc");
    vfs_mount_fs("/dev", "devtmpfs");
    ksysfs_init();

    const char *init = get_init_process();
    if (run_init_process(init) != 0) {
        const char *fallbacks[] = {
            "/sbin/init",
            "/etc/init",
            "/bin/init",
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

static void kernel_init(void)
{
    do_basic_setup();
    prepare_namespace();
}

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

void start_kernel(struct multiboot_info* mbi, uint32_t magic)
{
    /* Initialize serial port FIRST so we can debug without VGA */
    init_serial();

    /* Initialize terminal vga interface */
    init_vga();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Invalid Multiboot magic number!");

    setup_arch(mbi);

    /* Initialize the entire Memory Management subsystem (PMM, VMM, KHEAP) */
    init_mm(mbi);

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
    printf("Memory address 0xB8000 is directly manipulated.\n");
    printf("Enjoy your OS development journey!\n");

    /* Transition to rest_init and spawn PID 1 */
    rest_init();
}

__attribute__((section(".text.entry")))
void kernel_entry(uint32_t magic, struct multiboot_info* mbi)
{
    start_kernel(mbi, magic);
    panic(NULL);
}
