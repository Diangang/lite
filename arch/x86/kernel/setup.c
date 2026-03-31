#include "asm/gdt.h"
#include "asm/idt.h"
#include "asm/multiboot.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"

/*
 * In Linux, architecture-specific code (like arch/x86/kernel/setup.c or board-*.c)
 * is responsible for telling the core kernel what hardware devices exist on the board
 * by registering platform devices.
 */
static int x86_platform_devices_init(void)
{
    struct bus_type *platform = device_model_platform_bus();
    if (!platform)
        return -1;

    // Register static hardware devices that exist on this "board"
    struct class *cls = device_model_console_class();
    device_register_simple_class("console", "console", platform, cls, NULL);
    struct class *tty_cls = device_model_tty_class();
    device_register_simple_class("tty", "tty", platform, tty_cls, NULL);
    device_register_simple_class("ttyS0", "serial", platform, tty_cls, NULL);

    return 0;
}
subsys_initcall(x86_platform_devices_init);

void setup_arch(struct multiboot_info* mbi)
{
    (void)mbi;
    init_gdt();
    init_idt();
}

void start_kernel(struct multiboot_info* mbi, uint32_t magic);

__attribute__((section(".text.entry")))
void kernel_entry(uint32_t magic, struct multiboot_info* mbi)
{
    start_kernel(mbi, magic);
    panic(NULL);
}
