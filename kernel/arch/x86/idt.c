#include "idt.h"
#include <string.h> /* For memset */

/* Declare an IDT of 256 entries */
idt_entry_t idt_entries[256];
idt_ptr_t   idt_ptr;

/* Defined in idt_flush.s */
extern void idt_flush(uint32_t);

/*
 * Helper to set an entry in the IDT.
 * num: Interrupt vector number (0-255)
 * base: Address of the Interrupt Service Routine (ISR)
 * sel: Kernel code segment selector (0x08)
 * flags: 0x8E for Interrupt Gate
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt_entries[num].base_low  = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;

    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags   = flags;
}

void init_idt(void)
{
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    /* Initialize all IDT entries to zero first */
    memset(&idt_entries, 0, sizeof(idt_entry_t) * 256);

    /*
     * TODO: Add ISR handlers here later.
     * For now, the IDT is empty, so any interrupt will cause a Triple Fault.
     */

    /* Load the IDT pointer */
    idt_flush((uint32_t)&idt_ptr);
}
