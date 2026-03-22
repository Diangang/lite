.section .text
.global _start

.equ SYS_WRITE, 0
.equ SYS_EXIT, 3
.equ SYS_MMAP, 9

_start:
    mov $1, %ebx
    mov $banner, %ecx
    mov $banner_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    // Force a page fault by writing to NULL
    mov $1, %ebx
    mov $msg_pf, %ecx
    mov $msg_pf_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    movl $0xDEADBEEF, 0x00000000

    mov $1, %ebx
    mov $SYS_EXIT, %eax
    int $0x80

.section .rodata
banner:
    .ascii "ktest: Running kernel protection tests...\n"
banner_len = . - banner
msg_pf:
    .ascii "ktest: Triggering page fault (write to NULL)...\n"
msg_pf_len = . - msg_pf
