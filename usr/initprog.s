.section .text
.global _start

.equ SYS_WRITE, 0
.equ SYS_EXIT, 3
.equ SYS_EXECVE, 14
.equ SYS_WAITPID, 15
.equ SYS_FORK, 20

_start:
    mov $1, %ebx
    mov $banner, %ecx
    mov $banner_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

restart_shell:
    mov $SYS_FORK, %eax
    int $0x80
    cmp $0, %eax
    je child
    mov %eax, %esi

parent_wait:
    mov %esi, %ebx
    mov $waitbuf, %ecx
    mov $16, %edx
    mov $SYS_WAITPID, %eax
    int $0x80
    jmp restart_shell

child:
    mov $ush_path, %ebx
    mov $SYS_EXECVE, %eax
    int $0x80

    mov $1, %ebx
    mov $fail, %ecx
    mov $fail_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    mov $1, %ebx
    mov $SYS_EXIT, %eax
    int $0x80

.section .rodata
banner:
    .ascii "init: fork+exec /initrd/ush.elf\n"
banner_len = . - banner
fail:
    .ascii "init: exec failed\n"
fail_len = . - fail
ush_path:
    .ascii "/initrd/ush.elf\0"

.section .bss
waitbuf:
    .space 16
