#ifndef ASM_HARDIRQ_H
#define ASM_HARDIRQ_H

#include <stdint.h>

/*
 * Linux mapping: arch/x86/include/asm/hardirq.h
 *
 * Lite is single-CPU, so we keep one global irq_cpustat_t instance instead of
 * Linux per-cpu storage, but preserve the field names and access pattern.
 */
typedef struct {
    unsigned int apic_timer_irqs;
    unsigned int irq_spurious_count;
    unsigned int irq_resched_count;
    unsigned int irq_call_count;
} irq_cpustat_t;

extern irq_cpustat_t irq_stat;

#define inc_irq_stat(member) (++irq_stat.member)

#endif
