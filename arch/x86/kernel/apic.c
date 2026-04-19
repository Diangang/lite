#include "asm/apic.h"
#include "asm/irq_vectors.h"
#include "linux/interrupt.h"
#include "linux/panic.h"
#include "linux/printk.h"
#include "linux/sched.h"

/*
 * Linux mapping: local APIC enablement lives in arch/x86/kernel/apic/apic.c.
 * Lite keeps an explicit arch-owned placeholder so future LAPIC/SMP work does
 * not leak into generic IRQ helpers or the legacy i8259 path.
 */
int pic_mode = 1;
static int lapic_enabled;
static uint32_t apic_timer_irqs;
static uint32_t irq_resched_count;
static uint32_t irq_call_count;
static uint32_t irq_error_count;
static uint32_t irq_spurious_count;
static uint32_t apic_last_reschedule_vector;
static uint32_t apic_last_call_vector;
static uint32_t apic_last_error_vector;
static uint32_t apic_last_spurious_vector;

static struct pt_regs *apic_handle_local_timer(struct pt_regs *regs)
{
    apic_timer_irqs++;
    if (pic_mode)
        panic("local APIC timer fired while PIC mode is active.");
    if (!apic_enabled())
        panic("local APIC timer fired before local APIC runtime was enabled.");
    panic("local APIC timer interrupt path not implemented.");
    return regs;
}

static struct pt_regs *apic_handle_reschedule_ipi(struct pt_regs *regs)
{
    irq_resched_count++;
    apic_last_reschedule_vector = regs ? regs->int_no : RESCHEDULE_VECTOR;
    if (pic_mode)
        panic("reschedule IPI fired while PIC mode is active.");
    if (!apic_enabled())
        panic("reschedule IPI fired before local APIC runtime was enabled.");
    panic("reschedule IPI path not implemented.");
    return regs;
}

static struct pt_regs *apic_handle_call_function_ipi(struct pt_regs *regs)
{
    irq_call_count++;
    apic_last_call_vector = regs ? regs->int_no : CALL_FUNCTION_VECTOR;
    if (pic_mode)
        panic("call-function IPI fired while PIC mode is active.");
    if (!apic_enabled())
        panic("call-function IPI fired before local APIC runtime was enabled.");
    panic("call-function IPI path not implemented.");
    return regs;
}

static struct pt_regs *apic_handle_error_event(struct pt_regs *regs)
{
    irq_error_count++;
    apic_last_error_vector = regs ? regs->int_no : ERROR_APIC_VECTOR;
    if (pic_mode)
        panic("local APIC error interrupt fired while PIC mode is active.");
    if (!apic_enabled())
        panic("local APIC error interrupt fired before local APIC runtime was enabled.");
    panic("local APIC error interrupt path not implemented.");
    return regs;
}

static struct pt_regs *apic_handle_spurious_event(struct pt_regs *regs)
{
    apic_last_spurious_vector = regs ? regs->int_no : SPURIOUS_APIC_VECTOR;
    irq_spurious_count++;
    if (pic_mode)
        panic("spurious APIC interrupt fired while PIC mode is active.");
    if (!apic_enabled())
        panic("spurious APIC interrupt fired before local APIC runtime was enabled.");
    panic("spurious APIC interrupt path not implemented.");
    return regs;
}

int apic_init(void)
{
    pic_mode = 1;
    lapic_enabled = 0;
    return 0;
}

int apic_enabled(void)
{
    return lapic_enabled;
}

static void apic_install_local_timer_vector(void)
{
    register_interrupt_handler(LOCAL_TIMER_VECTOR, apic_handle_local_timer);
}

static void apic_install_ipi_vectors(void)
{
    register_interrupt_handler(RESCHEDULE_VECTOR, apic_handle_reschedule_ipi);
    register_interrupt_handler(CALL_FUNCTION_VECTOR, apic_handle_call_function_ipi);
}

static void apic_install_local_event_vectors(void)
{
    register_interrupt_handler(ERROR_APIC_VECTOR, apic_handle_error_event);
    register_interrupt_handler(SPURIOUS_APIC_VECTOR, apic_handle_spurious_event);
}

void apic_install_interrupts(void)
{
    apic_install_local_timer_vector();
    apic_install_ipi_vectors();
    apic_install_local_event_vectors();
}
