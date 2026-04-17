#ifndef ASM_IO_APIC_H
#define ASM_IO_APIC_H

/*
 * Linux mapping: arch/x86/kernel/apic/io_apic.c owns I/O APIC routing and
 * external interrupt-controller setup. Lite keeps only a placeholder boundary
 * until APIC/IOAPIC routing exists.
 */
int io_apic_init(void);
int io_apic_enabled(void);

#endif
