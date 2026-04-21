#include "asm/irqflags.h"
#include "asm/desc.h"
#include "asm/apic.h"
#include "asm/i8259.h"
#include "asm/io_apic.h"
#include "asm/irq_vectors.h"
#include "linux/interrupt.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/sched.h"

static struct irq_desc irq_descs[NR_IRQS];
static struct irq_desc *vector_irq[256];
static uint8_t irq_legacy_vectors[NR_IRQS];

/* IRQ stubs (defined in arch/x86/entry/entry_32.S). */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

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
    return vector_irq[vector];
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
    vector_irq[desc->vector] = desc;
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

/*
 * Linux mapping: IRQ mode setup/dispatch live under arch/x86/kernel/irq*.c.
 * Lite keeps a PIC-only subset and relies on IDT programming from traps.c.
 */

struct pt_regs *irq_handler(struct pt_regs *regs)
{
    struct irq_desc *desc = irq_desc_from_vector((uint8_t)regs->int_no);
    if (!desc) {
        printk("Unexpected IRQ vector %d\n", regs->int_no);
        panic("Unhandled IRQ vector.");
    }

    regs = irq_dispatch(regs);

    if (desc->irq == IRQ_TIMER) {
        task_tick();
        if (task_should_resched())
            regs = task_schedule(regs);
    }

    return regs;
}

void irq_install(void)
{
    static void (*const irq_stubs[NR_IRQS])(void) = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15,
    };

    if (apic_init() != 0)
        panic("local APIC placeholder init failed.");
    if (io_apic_init() != 0)
        panic("I/O APIC placeholder init failed.");
    if (!pic_mode) {
        if (apic_enabled() || io_apic_enabled())
            panic("APIC interrupt mode not implemented.");
        panic("non-PIC interrupt mode is not implemented.");
    }
    if (i8259_init() != 0)
        panic("i8259 init failed.");

    irq_init_legacy_vectors();

    for (uint32_t irq = 0; irq < NR_IRQS; irq++) {
        irq_set_chip_and_handler(irq, i8259_get_chip(), NULL);
        idt_set_gate(irq_to_vector(irq), (uint32_t)(uintptr_t)irq_stubs[irq], 0x08, 0x8E);
    }
}
