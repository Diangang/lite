#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Initialization function */
void init_gdt(void);
void tss_set_kernel_stack(uint32_t stack);

#endif
