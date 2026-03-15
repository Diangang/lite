.section .text
.global idt_flush
.type idt_flush, @function

idt_flush:
    /* Get the pointer to the IDT, passed as a parameter. */
    mov 4(%esp), %eax
    lidt (%eax)
    ret
