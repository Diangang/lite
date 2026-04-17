#include "asm/i8259.h"
#include "linux/interrupt.h"
#include "linux/libc.h"

#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_IMR  0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_IMR   0xA1

static uint16_t cached_irq_mask = 0xFFFFu;

static void i8259_write_masks(void)
{
    outb(PIC_MASTER_IMR, (uint8_t)(cached_irq_mask & 0xFFu));
    outb(PIC_SLAVE_IMR, (uint8_t)((cached_irq_mask >> 8) & 0xFFu));
}

static void i8259_irq_mask(unsigned int irq)
{
    if (irq >= NR_IRQS)
        return;
    cached_irq_mask |= (uint16_t)(1u << irq);
    i8259_write_masks();
}

static void i8259_irq_unmask(unsigned int irq)
{
    if (irq >= NR_IRQS)
        return;
    cached_irq_mask &= (uint16_t)~(1u << irq);
    i8259_write_masks();
}

static void i8259_irq_ack(unsigned int irq)
{
    if (irq >= 8)
        outb(PIC_SLAVE_CMD, 0x20);
    outb(PIC_MASTER_CMD, 0x20);
}

static struct irq_chip i8259_chip = {
    .name = "i8259",
    .irq_mask = i8259_irq_mask,
    .irq_unmask = i8259_irq_unmask,
    .irq_ack = i8259_irq_ack,
};

struct irq_chip *i8259_get_chip(void)
{
    return &i8259_chip;
}

int i8259_init(void)
{
    /*
     * Linux mapping: legacy 8259A initialization/remap is handled in a dedicated
     * i8259 layer rather than from generic IRQ dispatch.
     */
    outb(PIC_MASTER_CMD, 0x11);
    outb(PIC_SLAVE_CMD, 0x11);

    outb(PIC_MASTER_IMR, IRQ_VECTOR_BASE);
    outb(PIC_SLAVE_IMR, IRQ_VECTOR_BASE + 8);

    outb(PIC_MASTER_IMR, 0x04);
    outb(PIC_SLAVE_IMR, 0x02);

    outb(PIC_MASTER_IMR, 0x01);
    outb(PIC_SLAVE_IMR, 0x01);

    cached_irq_mask = 0xFFFFu;
    i8259_irq_unmask(IRQ_TIMER);
    i8259_irq_unmask(IRQ_KEYBOARD);
    i8259_irq_unmask(IRQ_COM1);
    return 0;
}
