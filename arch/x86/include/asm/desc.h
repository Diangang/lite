#ifndef ASM_DESC_H
#define ASM_DESC_H

#include <stdint.h>

void init_gdt(void);
void tss_set_kernel_stack(uint32_t stack);
void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
