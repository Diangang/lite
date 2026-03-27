#ifndef ASM_IRQFLAGS_H
#define ASM_IRQFLAGS_H

#include <stdint.h>

uint32_t irq_save(void);
void irq_restore(uint32_t flags);

#endif
