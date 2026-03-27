#ifndef ASM_GDT_H
#define ASM_GDT_H

#include <stdint.h>

void init_gdt(void);
void tss_set_kernel_stack(uint32_t stack);

#endif
