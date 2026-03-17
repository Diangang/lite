.section .text
.global _start

_start:
    mov $msg, %ebx
    mov $msg_len, %ecx
    mov $0, %eax
    int $0x80

    xor %ebx, %ebx
    mov $3, %eax
    int $0x80

halt:
    hlt
    jmp halt

.section .rodata
msg:
    .ascii "user bin ok\n"
msg_len = . - msg
