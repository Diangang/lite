.section .text
.global _start

_start:
    mov $filename, %ebx
    xor %ecx, %ecx
    mov $6, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je exit_fail
    mov %eax, %esi

loop:
    mov %esi, %ebx
    mov $buf, %ecx
    mov $128, %edx
    mov $4, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je exit_fail
    test %eax, %eax
    jz close_and_exit

    mov $1, %ebx
    mov $buf, %ecx
    mov %eax, %edx
    mov $0, %eax
    int $0x80
    jmp loop

close_and_exit:
    mov %esi, %ebx
    mov $8, %eax
    int $0x80
    xor %ebx, %ebx
    mov $3, %eax
    int $0x80

exit_fail:
    mov $1, %ebx
    mov $3, %eax
    int $0x80

.section .rodata
filename:
    .ascii "readme.txt\0"

.section .bss
buf:
    .space 128
