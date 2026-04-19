#include "asm/gdt.h"
#include "asm/idt.h"
#include "asm/multiboot.h"
#include "linux/platform_device.h"
#include "linux/init.h"
#include "linux/libc.h"

/*
 * Lite keeps a minimal x86 board-description shim here. Linux typically wires
 * board/platform devices through arch/x86 platform code and initcall hooks.
 */
static int x86_platform_devices_init(void)
{
    platform_device_register_simple("serial", 0);
    platform_device_register_simple("i8042", 0);
    platform_device_register_simple("console", 0);

    return 0;
}
device_initcall(x86_platform_devices_init);

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
