#ifndef LINUX_INTERRUPT_H
#define LINUX_INTERRUPT_H

#include <stdint.h>
#include "asm/ptrace.h"

typedef struct pt_regs *(*isr_t)(struct pt_regs *);

void isr_install(void);
struct pt_regs *isr_handler(struct pt_regs *regs);
void register_interrupt_handler(uint8_t n, isr_t handler);

void irq_install(void);
struct pt_regs *irq_handler(struct pt_regs *regs);

uint32_t isr_get_count(uint8_t vector);

enum {
    IRQ0 = 32,
    IRQ1 = 33,
    IRQ2 = 34,
    IRQ3 = 35,
    IRQ4 = 36,
    IRQ5 = 37,
    IRQ6 = 38,
    IRQ7 = 39
};

#endif
