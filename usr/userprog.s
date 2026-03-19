.section .text
.global _start

_start:
    mov $1, %ebx
    mov $msg, %ecx
    mov $msg_len, %edx
    mov $0, %eax
    int $0x80

loop:
    mov $0, %ebx
    mov $buf, %ecx
    mov $1, %edx
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
    mov $1, %ebx
    mov $0x08000000, %ecx
    mov $4, %edx
    mov $0, %eax
    int $0x80

    cmp $0xFFFFFFFF, %eax
    jne 2f
    mov $1, %ebx
    mov $badptr_reject, %ecx
    mov $badptr_reject_len, %edx
    mov $0, %eax
    int $0x80
2:

    mov $1, %ebx
    mov $buf, %ecx
    mov $1, %edx
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
