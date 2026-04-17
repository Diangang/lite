#include "asm/apic.h"

/*
 * Linux mapping: local APIC enablement lives in arch/x86/kernel/apic/apic.c.
 * Lite keeps an explicit arch-owned placeholder so future LAPIC/SMP work does
 * not leak into generic IRQ helpers or the legacy i8259 path.
 */
int pic_mode = 1;
static int lapic_enabled;

int apic_init(void)
{
    pic_mode = 1;
    lapic_enabled = 0;
    return 0;
}

int apic_enabled(void)
{
    return lapic_enabled;
}
