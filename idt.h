#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* IDT Entry Structure */
struct idt_entry_struct {
    uint16_t base_low;
    uint16_t sel;        /* Kernel segment selector */
    uint8_t  always0;    /* This must always be 0 */
    uint8_t  flags;      /* Flags: 0x8E = Present, Ring 0, 'Interrupt Gate' */
    uint16_t base_high;
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

/* IDT Pointer Structure */
struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
