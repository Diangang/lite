#include "linux/interrupt.h"
#include "asm/apic.h"
#include "asm/idt.h"
#include "asm/i8259.h"
#include "asm/io_apic.h"
#include "asm/irq_vectors.h"
#include "linux/panic.h"
#include "linux/sched.h"
#include "linux/exit.h"
#include "linux/printk.h"
#include "linux/serial.h"
#include "linux/console.h"

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
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();
extern void apic_timer_interrupt();
extern void call_function_interrupt();
extern void reschedule_interrupt();
extern void error_interrupt();
extern void spurious_interrupt();

/* register_interrupt_handler: Register interrupt handler. */
void register_interrupt_handler(uint8_t vector, isr_t handler)
{
    interrupt_handlers[vector] = handler;
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

static struct pt_regs *apic_placeholder_interrupt(struct pt_regs *regs)
{
    if (pic_mode)
        panic("APIC/IPI vector fired while PIC mode is active.");
    panic("APIC/IPI interrupt path not implemented.");
    return regs;
}

/* Common handler for all ISRs */
struct pt_regs *isr_handler(struct pt_regs *regs)
{
    if (regs->int_no < 256)
        interrupt_count[regs->int_no]++;

    if (interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        regs = handler(regs);
        return regs;
    }

    if (regs->int_no < 32 && ((regs->cs & 0x3) == 0x3)) {
        printk("\nUser Exception: %s\n", exception_messages[regs->int_no]);
        do_exit_reason(1, TASK_EXIT_EXCEPTION, regs->int_no, regs->eip);
        struct pt_regs *task_schedule(struct pt_regs *r);
        regs = task_schedule(regs);
        return regs;
    }

    /* Unhandled interrupt - Panic! */
    printk("\nKERNEL PANIC! Exception: %s\n",
           regs->int_no < 32 ? exception_messages[regs->int_no] : "Unknown Exception");
    panic("System Halted.");
    return regs;
}

/* Common handler for all IRQs */
struct pt_regs *irq_handler(struct pt_regs *regs)
{
    if (regs->int_no < 256)
        interrupt_count[regs->int_no]++;
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

/* isr_get_count: Implement ISR get count. */
uint32_t isr_get_count(uint8_t vector)
{
    return interrupt_count[vector];
}

/* isr_install: Implement ISR install. */
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

    /* Linux-shaped APIC/IPI vectors: installed but inactive in PIC mode. */
    idt_set_gate(LOCAL_TIMER_VECTOR, (uint32_t)apic_timer_interrupt, 0x08, 0x8E);
    idt_set_gate(CALL_FUNCTION_VECTOR, (uint32_t)call_function_interrupt, 0x08, 0x8E);
    idt_set_gate(RESCHEDULE_VECTOR, (uint32_t)reschedule_interrupt, 0x08, 0x8E);
    idt_set_gate(ERROR_APIC_VECTOR, (uint32_t)error_interrupt, 0x08, 0x8E);
    idt_set_gate(SPURIOUS_APIC_VECTOR, (uint32_t)spurious_interrupt, 0x08, 0x8E);

    register_interrupt_handler(LOCAL_TIMER_VECTOR, apic_placeholder_interrupt);
    register_interrupt_handler(CALL_FUNCTION_VECTOR, apic_placeholder_interrupt);
    register_interrupt_handler(RESCHEDULE_VECTOR, apic_placeholder_interrupt);
    register_interrupt_handler(ERROR_APIC_VECTOR, apic_placeholder_interrupt);
    register_interrupt_handler(SPURIOUS_APIC_VECTOR, apic_placeholder_interrupt);
}

/* irq_install: Implement IRQ install. */
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
        idt_set_gate(irq_to_vector(irq), (uint32_t)irq_stubs[irq], 0x08, 0x8E);
    }
}
