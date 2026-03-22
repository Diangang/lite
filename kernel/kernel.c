#include "multiboot.h"
#include "libc.h"
#include "serial.h"
#include "vga.h"
#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "mm.h"
#include "vfs.h"
#include "ramfs.h"
#include "initrd.h"
#include "procfs.h"
#include "devfs.h"
#include "sysfs.h"
#include "syscall.h"
#include "keyboard.h"
#include "timer.h"
#include "task.h"
#include "device_model.h"
#include "tty.h"
#include "shell.h"

void kernel_main(struct multiboot_info* mbi, uint32_t magic)
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

    /* Initialize the VFS subsystem */
    init_vfs();

    /* Mount core filesystems */
    struct vfs_inode *ram_root = init_ramfs();
    struct vfs_inode *initrd_root = init_initrd(mbi);
    init_procfs();
    init_devfs();
    init_sysfs();

    device_model_init(ram_root, initrd_root);

    /* Validate root chdir after all mounts are done */
    vfs_chdir("/");
    printf("File System initialized.\n");

    /* Initialize System Calls */
    init_syscall();
    printf("Syscall handler installed.\n");

    /* Initialize Keyboard Driver */
    init_keyboard();

    /* Initialize PIT Timer (100 Hz = 10ms per tick) */
    init_timer(100);

    /* Initialize basic tasking */
    init_task();

    /* Enable Interrupts */
    __asm__ volatile ("sti");

	/* Newline support is rudimentary in this example */
	printf("Hello, Kernel World!\n");
	printf("This is a minimal kernel running on QEMU.\n");
	printf("Memory address 0xB8000 is directly manipulated.\n");
	printf("Enjoy your OS development journey!\n");

    /* Initialize the shell */
    init_shell();
    int init_pid = task_create_user("/initrd/init.elf");
    if (init_pid < 0)
        shell_set_foreground(0), printf("init.elf not found, staying in kernel shell.\n");
    else
        shell_set_foreground_pid((uint32_t)init_pid), printf("init task created.\n");
    task_create(shell_task);

    /* Infinite loop to keep the kernel running and responsive to interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((section(".text.entry")))
void kernel_entry(uint32_t magic, struct multiboot_info* mbi)
{
    outb(0xE9, 'K');
    kernel_main(mbi, magic);
    panic(NULL);
}
