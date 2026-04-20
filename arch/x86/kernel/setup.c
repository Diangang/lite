#include "asm/gdt.h"
#include "asm/idt.h"
#include "asm/multiboot.h"
#include "linux/libc.h"

/* setup_arch: Set up arch. */
void setup_arch(struct multiboot_info* mbi)
{
    (void)mbi;
    init_gdt();
    init_idt();
}

/* i386_start_kernel: x86_32 entry trampoline before generic start_kernel(). */
__attribute__((section(".text.entry")))
void i386_start_kernel(uint32_t magic, struct multiboot_info* mbi)
{
    start_kernel(mbi, magic);
    panic(NULL);
}
