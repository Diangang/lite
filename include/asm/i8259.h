#ifndef ASM_I8259_H
#define ASM_I8259_H

struct irq_chip;

int i8259_init(void);
struct irq_chip *i8259_get_chip(void);

#endif
