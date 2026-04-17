/*
 * interrupt.s - Low level interrupt handling
 */

.section .text
.extern isr_handler
.extern irq_handler

/* Common stub for ISRs (CPU Exceptions) */
isr_common_stub:
    pusha           /* Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax */

    mov %ds, %ax
    push %eax       /* Save Data Segment descriptor */

    mov $0x10, %ax  /* Load Kernel Data Segment descriptor */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp       /* Pass pointer to stack */
    call isr_handler
    mov %eax, %esp  /* Use the returned register pointer as new stack */

    pop %ebx        /* Restore Original Data Segment descriptor */
    mov %bx, %ds
    mov %bx, %es
    mov %bx, %fs
    mov %bx, %gs

    popa            /* Pops edi,esi,ebp... */
    add $8, %esp    /* Cleans up the pushed error code and pushed ISR number */
    iret            /* Returns 32-bit: pops cs, eip, eflags, ss, esp */

/* Common stub for IRQs (Hardware Interrupts) */
irq_common_stub:
    pusha

    mov %ds, %ax
    push %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp       /* Pass pointer to stack */
    call irq_handler
    mov %eax, %esp

    pop %ebx
    mov %bx, %ds
    mov %bx, %es
    mov %bx, %fs
    mov %bx, %gs

    popa
    add $8, %esp
    iret

/* IRQ stubs for the full legacy PIC range (IRQ0-IRQ15). */
.macro IRQ_STUB irq, vector
  .global irq\irq
irq\irq:
    push $0
    push $\vector
    jmp irq_common_stub
.endm

IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

/* Linux-shaped APIC/IPI placeholder vectors. */
.macro VECTOR_STUB label, vector
  .global \label
\label:
    push $0
    push $\vector
    jmp isr_common_stub
.endm

VECTOR_STUB apic_timer_interrupt, 0xef
VECTOR_STUB call_function_interrupt, 0xfc
VECTOR_STUB reschedule_interrupt, 0xfd
VECTOR_STUB error_interrupt, 0xfe
VECTOR_STUB spurious_interrupt, 0xff

/* ISRs (Exceptions) */
.macro ISR_NOERRCODE num
  .global isr\num
  isr\num:
    cli
    push $0
    push $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
  .global isr\num
  isr\num:
    cli
    push $\num
    jmp isr_common_stub
.endm

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

/* Syscall (int 0x80): do not implicitly disable interrupts */
.global isr128
/* isr128: Implement isr128. */
isr128:
    push $0
    push $128
    jmp isr_common_stub
