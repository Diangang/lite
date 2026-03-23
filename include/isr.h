#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* Registers structure passed to ISR handler */
struct registers {
    uint32_t ds;                                     /* Data segment selector */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* Pushed by pusha */
    uint32_t int_no, err_code;                       /* Interrupt number and error code */
    uint32_t eip, cs, eflags, useresp, ss;           /* Pushed by the processor automatically */
};

/* Function pointer for interrupt handlers */
typedef struct registers *(*isr_t)(struct registers *);

/* Public API */
void isr_install(void);
struct registers *isr_handler(struct registers *regs);
void register_interrupt_handler(uint8_t n, isr_t handler);

/* IRQ definitions */
void irq_install(void);
struct registers *irq_handler(struct registers *regs);

uint32_t isr_get_count(uint8_t vector);

#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39

#endif
