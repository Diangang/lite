#include "linux/libc.h" /* For memset, printf */
#include "linux/interrupt.h"

/* IDT Entry Structure */
struct idt_entry {
    uint16_t base_low;
    uint16_t sel;        /* Kernel segment selector */
    uint8_t  always0;    /* This must always be 0 */
    uint8_t  flags;      /* Flags: 0x8E = Present, Ring 0, 'Interrupt Gate' */
    uint16_t base_high;
} __attribute__((packed));

/* IDT Pointer Structure */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Declare an IDT of 256 entries */
struct idt_entry idt_entries[256];
struct idt_ptr   idt_ptr;

/*
 * Helper to set an entry in the IDT.
 * num: Interrupt vector number (0-255)
 * base: Address of the Interrupt Service Routine (ISR)
 * sel: Kernel code segment selector (0x08)
 * flags: 0x8E for Interrupt Gate
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt_entries[num].base_low  = (base & 0xFFFF);
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel       = sel;
    idt_entries[num].always0   = 0;
    idt_entries[num].flags     = flags;
}

static inline void idt_flush(uint32_t idt_ptr_addr)
{
    __asm__ volatile("lidt (%0)" : : "r"(idt_ptr_addr) : "memory");
}

void init_idt(void)
{
    idt_ptr.limit = sizeof(struct idt_entry) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    /* Initialize all IDT entries to zero first */
    memset(&idt_entries, 0, sizeof(struct idt_entry) * 256);

    /* Install CPU Exceptions */
    isr_install();

    /* Initialize Interrupt Service Routines (PIC remap + IRQ handlers) */
    irq_install();

    /* Load the IDT pointer */
    idt_flush((uint32_t)&idt_ptr);
    printf("IDT and Interrupts initialized.\n");
}
