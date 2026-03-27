#ifndef ASM_IDT_H
#define ASM_IDT_H

#include <stdint.h>

void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
