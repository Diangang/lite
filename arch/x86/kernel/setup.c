#include "asm/desc.h"
#include "asm/setup.h"

/* setup_arch: Set up arch. */
void setup_arch(struct multiboot_info* mbi)
{
    (void)mbi;
    init_gdt();
    init_idt();
}
