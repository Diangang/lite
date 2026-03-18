.section .text
.global _start

_start:
    mov $msg, %ebx
    mov $msg_len, %ecx
    mov $0, %eax
    int $0x80

loop:
    mov $buf, %ebx
    mov $1, %ecx
    mov $4, %eax
    int $0x80

    cmpb $'q', buf
    je exit

    cmpb $'!', buf
    jne 1f
    movl $0xDEADBEEF, 0x08000000
1:

    cmpb $'b', buf
    jne 2f
    mov $0x08000000, %ebx
    mov $4, %ecx
    mov $0, %eax
    int $0x80

    cmp $0xFFFFFFFF, %eax
    jne 2f
    mov $badptr_reject, %ebx
    mov $badptr_reject_len, %ecx
    mov $0, %eax
    int $0x80
2:

    mov $buf, %ebx
    mov $1, %ecx
    mov $0, %eax
    int $0x80

    jmp loop

exit:
    xor %ebx, %ebx
    mov $3, %eax
    int $0x80

halt:
    hlt
    jmp halt

.section .rodata
msg:
    .ascii "user bin ok (q exit, ! pf, b badptr)\n"
msg_len = . - msg
badptr_reject:
    .ascii "bad ptr rejected\n"
badptr_reject_len = . - badptr_reject

.section .bss
buf:
    .space 1
