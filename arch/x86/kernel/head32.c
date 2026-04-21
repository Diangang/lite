#include "asm/multiboot.h"
#include "linux/libc.h"
#include "linux/panic.h"
#include "linux/start_kernel.h"

/*
 * Bootloader handoff state (Multiboot).
 *
 * Linux mapping: boot-time x86 entry glue lives in arch/x86/kernel/head32.c,
 * while generic arch setup stays in setup.c.
 */
struct multiboot_info boot_mbi;

/* i386_start_kernel: x86_32 entry trampoline before generic start_kernel(). */
__attribute__((section(".text.entry")))
void i386_start_kernel(uint32_t magic, struct multiboot_info *mbi)
{
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Invalid Multiboot magic number!");
    boot_mbi = *mbi;
    start_kernel();
    panic(NULL);
}
