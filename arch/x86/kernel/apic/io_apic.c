#include "asm/io_apic.h"
#include "asm/apic.h"

/*
 * Linux mapping: external interrupt routing via I/O APIC lives in
 * arch/x86/kernel/apic/io_apic.c. Lite keeps a no-op arch boundary so
 * controller ownership is explicit before real IOAPIC support exists.
 */

int io_apic_init(void)
{
    return 0;
}

int io_apic_enabled(void)
{
    return !pic_mode;
}
