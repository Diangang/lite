#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* Registers structure passed to ISR handler */
typedef struct registers
{
    uint32_t ds;                                     /* Data segment selector */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* Pushed by pusha */
    uint32_t int_no, err_code;                       /* Interrupt number and error code */
    uint32_t eip, cs, eflags, useresp, ss;           /* Pushed by the processor automatically */
} registers_t;

/* Function pointer for interrupt handlers */
typedef void (*isr_t)(registers_t *);

/* Public API */
void isr_install(void);
void isr_handler(registers_t *regs);
void register_interrupt_handler(uint8_t n, isr_t handler);

/* IRQ definitions */
void irq_install(void);
registers_t *irq_handler(registers_t *regs);

#define IRQ0 32
#define IRQ1 33

#endif
