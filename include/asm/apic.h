#ifndef ASM_APIC_H
#define ASM_APIC_H

#include "asm/ptrace.h"

/*
 * Linux mapping: arch/x86/kernel/apic/apic.c owns local APIC bringup and
 * local interrupt-controller state. Lite keeps only a placeholder boundary
 * here until SMP/LAPIC support is implemented.
 */
extern int pic_mode;

int apic_init(void);
int apic_enabled(void);
void apic_install_interrupts(void);

#endif
