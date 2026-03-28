#include "asm/multiboot.h"
#include "asm/gdt.h"
#include "asm/idt.h"
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
#include "linux/syscall.h"
#include "linux/timer.h"
#include "linux/sched.h"
#include "linux/fork.h"
#include "linux/binfmts.h"
#include "linux/device.h"
#include "linux/tty.h"

extern initcall_t __initcall_start[];
extern initcall_t __initcall_end[];

static void do_initcalls(void)
{
    initcall_t *call;
    for (call = __initcall_start; call < __initcall_end; call++)
        (*call)();
}

void populate_rootfs(struct multiboot_info *mbi);

static void kernel_init(void)
{
    /* Execute all registered module init functions (driver core, device tree, etc.) */
    do_initcalls();

    vfs_mount_fs("/proc", "proc");
    vfs_mount_fs("/dev", "devtmpfs");
    vfs_mount_fs("/sys", "sysfs");

    printf("kernel_init: exec /sbin/init as PID 1\n");
    if (task_exec_user("/sbin/init") != 0)
        panic("No init found. Try passing init= option to kernel.");
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

    /* Initialize Global Descriptor Table */
    init_gdt();

    /* Initialize Interrupt Descriptor Table, Exceptions, and IRQs */
    init_idt();

    /* Initialize the entire Memory Management subsystem (PMM, VMM, KHEAP) */
    init_mm(mbi);

    /* Initialize the Virtual File System (VFS) and mount rootfs */
    vfs_init();

    sched_init();
    fork_init();

    // Extract initramfs directly into the root ramfs
    populate_rootfs(mbi);

    driver_init();

    /* Initialize System Calls */
    init_syscall();
    printf("Syscall handler installed.\n");

    /* Initialize PIT Timer (100 Hz = 10ms per tick) */
    init_timer(100);

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
