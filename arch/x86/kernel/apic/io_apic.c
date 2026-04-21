#include "asm/io_apic.h"

/*
 * Linux mapping: external interrupt routing via I/O APIC lives in
 * arch/x86/kernel/apic/io_apic.c. Lite keeps a no-op arch boundary so
 * controller ownership is explicit before real IOAPIC support exists.
 */
static int ioapic_enabled;

int io_apic_init(void)
{
    ioapic_enabled = 0;
    return 0;
}

int io_apic_enabled(void)
{
    return ioapic_enabled;
}
