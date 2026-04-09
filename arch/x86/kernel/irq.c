#include "asm/irqflags.h"

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
