.section .text
.global _start
.extern main

_start:
    call main
    # Exit with return value from main (in %eax)
    mov %eax, %ebx
    mov $3, %eax   # SYS_EXIT = 3
    int $0x80
