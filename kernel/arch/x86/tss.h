#ifndef TSS_H
#define TSS_H

#include <stdint.h>

void tss_init(void);
void tss_set_kernel_stack(uint32_t stack);
void tss_flush(uint16_t selector);

#endif
