.section .text
.global _start

.equ SYS_WRITE, 0
.equ SYS_READ, 4
.equ SYS_OPEN, 6
.equ SYS_CLOSE, 8
.equ SYS_EXIT, 3
.equ SYS_MMAP, 18
.equ SYS_MUNMAP, 19

_start:
    mov $1, %ebx
    mov $banner, %ecx
    mov $banner_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    xor %ebx, %ebx
    mov $0x2000, %ecx
    mov $0x3, %edx
    mov $SYS_MMAP, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je mmap_fail
    mov %eax, %esi

    movb $'M', 0(%esi)
    movb $'M', 1(%esi)
    movb $'A', 2(%esi)
    movb $'P', 3(%esi)
    movb $'\n', 4(%esi)
    mov $1, %ebx
    mov %esi, %ecx
    mov $5, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    mov $path, %ebx
    xor %ecx, %ecx
    mov $SYS_OPEN, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je dump_done
    mov %eax, %edi

dump_loop:
    mov %edi, %ebx
    mov $buf, %ecx
    mov $256, %edx
    mov $SYS_READ, %eax
    int $0x80
    cmp $0, %eax
    je dump_close
    cmp $0xFFFFFFFF, %eax
    je dump_close
    mov %eax, %edx
    mov $1, %ebx
    mov $buf, %ecx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp dump_loop

dump_close:
    mov %edi, %ebx
    mov $SYS_CLOSE, %eax
    int $0x80

dump_done:
    mov %esi, %ebx
    mov $0x2000, %ecx
    mov $SYS_MUNMAP, %eax
    int $0x80

    mov $1, %ebx
    mov $done, %ecx
    mov $done_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    xor %ebx, %ebx
    mov $SYS_EXIT, %eax
    int $0x80

mmap_fail:
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
    .ascii "mmap test start\n"
banner_len = . - banner
done:
    .ascii "mmap test done\n"
done_len = . - done
fail:
    .ascii "mmap failed\n"
fail_len = . - fail
path:
    .ascii "/proc/self/maps\0"

.section .bss
buf:
    .space 256
