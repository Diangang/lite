#include "isr.h"
#include "idt.h"
#include "kernel.h" /* For terminal_writestring and outb */
#include "task.h"

isr_t interrupt_handlers[256];
static uint32_t interrupt_count[256];

/* Defined in interrupt.s */
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void isr128();

/* ... we will add more as needed, but for now let's focus on IRQ1 (Keyboard) */
extern void irq0();
extern void irq1();
extern void irq4();

/*
 * Remap the PIC (Programmable Interrupt Controller)
 * This moves IRQs 0-7 to IDT entries 32-39, and IRQs 8-15 to 40-47.
 * This is necessary because IDT entries 0-31 are reserved for CPU exceptions.
 */
static void pic_remap(void)
{
    /* Master - Command: 0x20, Data: 0x21 */
    /* Slave  - Command: 0xA0, Data: 0xA1 */

    /* ICW1: Start initialization */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    /* ICW2: Remap offset */
    outb(0x21, 0x20); /* Master IRQ0 starts at 32 (0x20) */
    outb(0xA1, 0x28); /* Slave IRQ8 starts at 40 (0x28) */

    /* ICW3: Cascade identity */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    /* ICW4: 8086 mode */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* OCW1: Unmask all interrupts (0x00 = enable all, 0xFF = disable all) */
    /* Only enable IRQ0 (Timer), IRQ1 (Keyboard), and IRQ4 (Serial) for now */
    outb(0x21, 0xEC); /* 1110 1100: Enable IRQ0, IRQ1, IRQ4 */
    outb(0xA1, 0xFF); /* Disable all slave IRQs */
}

void register_interrupt_handler(uint8_t n, isr_t handler)
{
    interrupt_handlers[n] = handler;
}

/* Exception messages */
char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

/* Common handler for all ISRs */
void isr_handler(registers_t *regs)
{
    if (regs->int_no < 256) {
        interrupt_count[regs->int_no]++;
    }
    if (interrupt_handlers[regs->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
    else
    {
        if (regs->int_no < 32 && ((regs->cs & 0x3) == 0x3)) {
            terminal_writestring("\nUser Exception: ");
            terminal_writestring(exception_messages[regs->int_no]);
            terminal_writestring("\n");
            serial_write("User Exception: ");
            serial_write(exception_messages[regs->int_no]);
            serial_write("\n");
            task_exit_with_reason(1, TASK_EXIT_EXCEPTION, regs->int_no, regs->eip);
            return;
        }
        /* Unhandled interrupt - Panic! */
        terminal_writestring("\nKERNEL PANIC! Exception: ");
        if (regs->int_no < 32) {
            terminal_writestring(exception_messages[regs->int_no]);
        } else {
            terminal_writestring("Unknown Exception");
        }
        terminal_writestring("\nSystem Halted.\n");
        while(1) {
            __asm__ volatile("cli; hlt");
        }
    }
}

/* Common handler for all IRQs */
registers_t *irq_handler(registers_t *regs)
{
    if (regs->int_no < 256) {
        interrupt_count[regs->int_no]++;
    }
    /* Send EOI (End of Interrupt) signal to PICs */
    /* If IRQ >= 8 (slave), send to slave PIC */
    if (regs->int_no >= 40)
    {
        outb(0xA0, 0x20);
    }
    /* Always send to master PIC */
    outb(0x20, 0x20);

    /* Handle Serial Interrupt specifically if needed, or through handler array */
    if (regs->int_no == 36) { // IRQ 4
        serial_handler(regs);
    }
    else if (interrupt_handlers[regs->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }

    if (regs->int_no == IRQ0) {
        registers_t *task_schedule(registers_t *r);
        void task_tick(void);
        task_tick();
        regs = task_schedule(regs);
    }

    return regs;
}

uint32_t isr_get_count(uint8_t vector)
{
    return interrupt_count[vector];
}

void isr_install(void)
{
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEF);
}

void irq_install(void)
{
    pic_remap();

    /* Install IRQ handlers in IDT */
    /* IRQ0 (Timer) -> IDT 32 */
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);

    /* IRQ1 (Keyboard) -> IDT 33 */
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);

    /* IRQ4 (Serial) -> IDT 36 */
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
}
