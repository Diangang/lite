#ifndef LINUX_INTERRUPT_H
#define LINUX_INTERRUPT_H

#include <stdint.h>
#include "asm/ptrace.h"

typedef struct pt_regs *(*isr_t)(struct pt_regs *);

/*
 * Linux mapping:
 * - irq_desc describes one Linux IRQ line.
 * - irq_chip provides controller-specific mask/unmask/ack callbacks.
 *
 * Lite keeps only the minimal fields needed for the legacy i8259 path.
 */
struct irq_chip {
    const char *name;
    void (*irq_mask)(unsigned int irq);
    void (*irq_unmask)(unsigned int irq);
    void (*irq_ack)(unsigned int irq);
};

struct irq_desc {
    uint8_t irq;
    uint8_t vector;
    struct irq_chip *chip;
    isr_t handler;
    uint32_t count;
};

void isr_install(void);
struct pt_regs *isr_handler(struct pt_regs *regs);
void register_interrupt_handler(uint8_t vector, isr_t handler);

void irq_install(void);
struct pt_regs *irq_handler(struct pt_regs *regs);
struct pt_regs *irq_dispatch(struct pt_regs *regs);
int register_irq_handler(unsigned int irq, isr_t handler);
void irq_set_chip_and_handler(unsigned int irq, struct irq_chip *chip, isr_t handler);
struct irq_desc *irq_to_desc(unsigned int irq);
struct irq_desc *irq_desc_from_vector(uint8_t vector);
void irq_mask(unsigned int irq);
void irq_unmask(unsigned int irq);

uint32_t isr_get_count(uint8_t vector);
uint32_t irq_get_count(unsigned int irq);
void irq_init_legacy_vectors(void);
uint8_t irq_to_vector(unsigned int irq);

enum {
    NR_IRQS = 16,
    IRQ_VECTOR_BASE = 32,
    IRQ_TIMER = 0,
    IRQ_KEYBOARD = 1,
    IRQ_CASCADE = 2,
    IRQ_COM2 = 3,
    IRQ_COM1 = 4
};

#endif
