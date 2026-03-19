.section .text
.global _start

.equ SYS_WRITE, 0
.equ SYS_EXIT, 3
.equ SYS_MMAP, 18
.equ SYS_FORK, 20

_start:
    mov $1, %ebx
    mov $start_msg, %ecx
    mov $start_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    xor %ebx, %ebx
    mov $0x1000, %ecx
    mov $0x3, %edx
    mov $SYS_MMAP, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je exit_ok
    mov %eax, %edi

    movl $0x11223344, 0(%edi)

    mov $SYS_FORK, %eax
    int $0x80
    cmp $0, %eax
    je child_path

parent_path:
    movl $0x55667788, 0(%edi)
    mov $1, %ebx
    mov $parent_msg, %ecx
    mov $parent_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp exit_ok

child_path:
    mov $1, %ebx
    mov $child_msg, %ecx
    mov $child_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    movl 0(%edi), %eax
    cmp $0x11223344, %eax
    jne child_bad
    mov $1, %ebx
    mov $child_ok, %ecx
    mov $child_ok_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp exit_ok

child_bad:
    mov $1, %ebx
    mov $child_bad_msg, %ecx
    mov $child_bad_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp exit_ok

exit_ok:
    xor %ebx, %ebx
    mov $SYS_EXIT, %eax
    int $0x80

.section .rodata
start_msg:
    .ascii "fork test start\n"
start_len = . - start_msg
parent_msg:
    .ascii "parent wrote\n"
parent_len = . - parent_msg
child_msg:
    .ascii "child sees\n"
child_len = . - child_msg
child_ok:
    .ascii "child ok\n"
child_ok_len = . - child_ok
child_bad_msg:
    .ascii "child bad\n"
child_bad_len = . - child_bad_msg
