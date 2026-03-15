.section .text
.global gdt_flush
.type gdt_flush, @function

gdt_flush:
    /* Get the pointer to the GDT, passed as a parameter. */
    mov 4(%esp), %eax
    lgdt (%eax)

    /*
     * Reload segment registers.
     * 0x10 is the offset in the GDT to our Data Segment (Index 2 -> 2*8 = 16 = 0x10)
     */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /*
     * Jump to Code Segment to reload CS register.
     * 0x08 is the offset to our Code Segment (Index 1 -> 1*8 = 8 = 0x08)
     */
    ljmp $0x08, $.flush
.flush:
    ret
