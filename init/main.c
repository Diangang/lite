#include "multiboot.h"
#include "libc.h"
#include "serial.h"
#include "vga.h"
#include "console.h"
#include "init.h"

extern initcall_t __initcall_start[];
extern initcall_t __initcall_end[];

static void do_initcalls(void)
{
    initcall_t *call;
    for (call = __initcall_start; call < __initcall_end; call++)
        (*call)();
}
#include "gdt.h"
#include "idt.h"
#include "mm.h"
#include "fs.h"
#include "ramfs.h"
#include "initrd.h"
#include "procfs.h"
#include "devfs.h"
#include "sysfs.h"
#include "syscall.h"
#include "timer.h"
#include "task.h"
#include "device_model.h"
#include "tty.h"

void populate_rootfs(struct multiboot_info *mbi);

static void kernel_init(void)
{
    /* Execute all registered module init functions (driver core, device tree, etc.) */
    do_initcalls();

    vfs_mount_fs("/proc", "proc");
    vfs_mount_fs("/dev", "devfs");
    vfs_mount_fs("/sys", "sysfs");

    /* Switch to user mode and start init process */
    /* We try standard Linux init paths in order */
    int init_pid;

    init_pid = task_create_user("/sbin/init");
    if (init_pid > 0) goto init_started;

    panic("No init found. Try passing init= option to kernel.");

init_started:
    printf("init task created (pid %d).\n", init_pid);

    /* The kernel_init thread has done its job. It can exit now,
       or just wait/idle if we don't have thread exit fully working yet. */
    while (1)
        task_sleep(100);
}

static void rest_init(void)
{
    /* 1. Create the kernel_init thread. It will become PID 1. */
    task_create(kernel_init);

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

    // Since initrd is gone, we don't pass its root to driver_init anymore.
    // The device model can just use the rootfs.
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
    outb(0xE9, 'K');
    start_kernel(mbi, magic);
    panic(NULL);
}
