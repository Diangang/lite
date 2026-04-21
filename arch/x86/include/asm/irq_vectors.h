#ifndef ASM_IRQ_VECTORS_H
#define ASM_IRQ_VECTORS_H

/*
 * Linux mapping: arch/x86/include/asm/irq_vectors.h
 *
 * These vectors are arch-owned and do not correspond to a physical IRQ pin.
 * Lite installs placeholder IDT entries for them to make future LAPIC/IPI work
 * land in Linux-shaped places, while keeping PIC mode as the only active mode.
 */

#define NR_VECTORS                  256

#define SPURIOUS_APIC_VECTOR        0xff
#define ERROR_APIC_VECTOR           0xfe
#define RESCHEDULE_VECTOR           0xfd
#define CALL_FUNCTION_VECTOR        0xfc

/* Local APIC timer IRQ vector */
#define LOCAL_TIMER_VECTOR          0xef

#endif
