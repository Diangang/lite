#include "asm/irqflags.h"
#include "linux/interrupt.h"

static struct irq_desc irq_descs[NR_IRQS];
static struct irq_desc *irq_vector_map[256];
static uint8_t irq_legacy_vectors[NR_IRQS];

void irq_init_legacy_vectors(void)
{
    for (uint32_t irq = 0; irq < NR_IRQS; irq++)
        irq_legacy_vectors[irq] = (uint8_t)(IRQ_VECTOR_BASE + irq);
}

uint8_t irq_to_vector(unsigned int irq)
{
    if (irq >= NR_IRQS)
        return 0;
    return irq_legacy_vectors[irq];
}

/* irq_save: Implement IRQ save. */
uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

/* irq_restore: Implement IRQ restore. */
void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
    if (irq >= NR_IRQS)
        return 0;
    return &irq_descs[irq];
}

struct irq_desc *irq_desc_from_vector(uint8_t vector)
{
    return irq_vector_map[vector];
}

void irq_set_chip_and_handler(unsigned int irq, struct irq_chip *chip, isr_t handler)
{
    struct irq_desc *desc = irq_to_desc(irq);
    if (!desc)
        return;
    desc->irq = (uint8_t)irq;
    desc->vector = irq_to_vector(irq);
    desc->chip = chip;
    desc->handler = handler;
    irq_vector_map[desc->vector] = desc;
}

int register_irq_handler(unsigned int irq, isr_t handler)
{
    struct irq_desc *desc = irq_to_desc(irq);
    if (!desc)
        return -1;
    desc->handler = handler;
    return 0;
}

void irq_mask(unsigned int irq)
{
    struct irq_desc *desc = irq_to_desc(irq);
    if (!desc || !desc->chip || !desc->chip->irq_mask)
        return;
    desc->chip->irq_mask(irq);
}

void irq_unmask(unsigned int irq)
{
    struct irq_desc *desc = irq_to_desc(irq);
    if (!desc || !desc->chip || !desc->chip->irq_unmask)
        return;
    desc->chip->irq_unmask(irq);
}

struct pt_regs *irq_dispatch(struct pt_regs *regs)
{
    if (!regs)
        return regs;
    struct irq_desc *desc = irq_desc_from_vector((uint8_t)regs->int_no);
    if (!desc)
        return regs;
    desc->count++;
    if (desc->chip && desc->chip->irq_ack)
        desc->chip->irq_ack(desc->irq);
    if (desc->handler)
        return desc->handler(regs);
    return regs;
}

uint32_t irq_get_count(unsigned int irq)
{
    struct irq_desc *desc = irq_to_desc(irq);
    if (!desc)
        return 0;
    return desc->count;
}
