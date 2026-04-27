#include "asm/desc.h"
#include "asm/apic.h"
#include "asm/irq_vectors.h"

#include "linux/interrupt.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/string.h"

/*
 * Linux mapping: exception/trap setup and early IDT programming live under
 * arch/x86/kernel/traps.c. Lite implements a small subset but follows
 * Linux file placement.
 */

/*
 * IDT gate descriptor.
 *
 * Linux mapping (i386): typedef struct desc_struct gate_desc; in
 * linux2.6/arch/x86/include/asm/desc_defs.h. Lite keeps the x86 i386
 * gate-shaped packed fields as an explicit subset of Linux's union form.
 */
typedef struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed)) gate_desc;

/*
 * IDTR structure.
 * Linux mapping: struct desc_ptr in linux2.6/arch/x86/include/asm/desc_defs.h.
 */
struct desc_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static gate_desc idt_table[256];
static struct desc_ptr lite_idt_descr;

static isr_t interrupt_handlers[256];
static uint32_t interrupt_count[256];

/* Exception stubs (defined in arch/x86/entry/entry_32.S). */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr128(void);

/* APIC/IPI vectors (also defined in entry_32.S). */
extern void apic_timer_interrupt(void);
extern void call_function_interrupt(void);
extern void reschedule_interrupt(void);
extern void error_interrupt(void);
extern void spurious_interrupt(void);

static inline void idt_flush(uint32_t idt_ptr_addr)
{
    __asm__ volatile("lidt (%0)" : : "r"(idt_ptr_addr) : "memory");
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt_table[num].base_low = (uint16_t)(base & 0xFFFFu);
    idt_table[num].base_high = (uint16_t)((base >> 16) & 0xFFFFu);
    idt_table[num].sel = sel;
    idt_table[num].always0 = 0;
    idt_table[num].flags = flags;
}

void register_interrupt_handler(uint8_t vector, isr_t handler)
{
    interrupt_handlers[vector] = handler;
}

static const char *const exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
};

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

    printk("\nKERNEL PANIC! Exception: %s\n",
           regs->int_no < 32 ? exception_messages[regs->int_no] : "Unknown Exception");
    panic("System Halted.");
    return regs;
}

uint32_t isr_get_count(uint8_t vector)
{
    return interrupt_count[vector];
}

void isr_install(void)
{
    idt_set_gate(0, (uint32_t)(uintptr_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)(uintptr_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)(uintptr_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)(uintptr_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)(uintptr_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)(uintptr_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)(uintptr_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)(uintptr_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)(uintptr_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)(uintptr_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)(uintptr_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)(uintptr_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)(uintptr_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)(uintptr_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)(uintptr_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)(uintptr_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)(uintptr_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)(uintptr_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)(uintptr_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)(uintptr_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)(uintptr_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)(uintptr_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)(uintptr_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)(uintptr_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)(uintptr_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)(uintptr_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)(uintptr_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)(uintptr_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)(uintptr_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)(uintptr_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)(uintptr_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)(uintptr_t)isr31, 0x08, 0x8E);
    idt_set_gate(128, (uint32_t)(uintptr_t)isr128, 0x08, 0xEF);

    idt_set_gate(LOCAL_TIMER_VECTOR, (uint32_t)(uintptr_t)apic_timer_interrupt, 0x08, 0x8E);
    idt_set_gate(CALL_FUNCTION_VECTOR, (uint32_t)(uintptr_t)call_function_interrupt, 0x08, 0x8E);
    idt_set_gate(RESCHEDULE_VECTOR, (uint32_t)(uintptr_t)reschedule_interrupt, 0x08, 0x8E);
    idt_set_gate(ERROR_APIC_VECTOR, (uint32_t)(uintptr_t)error_interrupt, 0x08, 0x8E);
    idt_set_gate(SPURIOUS_APIC_VECTOR, (uint32_t)(uintptr_t)spurious_interrupt, 0x08, 0x8E);

    apic_install_interrupts();
}

void init_idt(void)
{
    lite_idt_descr.limit = (uint16_t)(sizeof(gate_desc) * 256u - 1u);
    lite_idt_descr.base = (uint32_t)(uintptr_t)&idt_table;
    memset(&idt_table, 0, sizeof(idt_table));

    isr_install();
    irq_install();

    idt_flush((uint32_t)(uintptr_t)&lite_idt_descr);
    printf("IDT and Interrupts initialized.\n");
}
