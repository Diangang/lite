.section .text
.global _start

.equ SYS_WRITE, 0
.equ SYS_EXIT, 3
.equ SYS_READ, 4
.equ SYS_OPEN, 6
.equ SYS_CLOSE, 8
.equ SYS_CHDIR, 10
.equ SYS_GETCWD, 11
.equ SYS_GETDENT, 12
.equ SYS_GETDENTS, 21
.equ SYS_MKDIR, 13
.equ SYS_EXECVE, 14

.equ O_CREAT, (1<<6)
.equ O_TRUNC, (1<<9)

_start:
    mov $1, %ebx
    mov $banner, %ecx
    mov $banner_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

main_loop:
    mov $1, %ebx
    mov $prompt, %ecx
    mov $prompt_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    mov $linebuf, %edi
    mov $0, %esi

read_line:
    mov $0, %ebx
    mov $chbuf, %ecx
    mov $1, %edx
    mov $SYS_READ, %eax
    int $0x80
    cmp $0, %eax
    je main_loop
    cmp $0xFFFFFFFF, %eax
    je main_loop
    mov chbuf, %al
    cmp $'\r', %al
    je end_line
    cmp $'\n', %al
    je end_line
    cmp $127, %al
    je backspace
    cmp $'\b', %al
    je backspace
    cmp $255, %esi
    jae read_line
    mov %al, (%edi,%esi,1)
    inc %esi
    jmp read_line

backspace:
    cmp $0, %esi
    je read_line
    dec %esi
    jmp read_line

end_line:
    movb $0, (%edi,%esi,1)

    mov %edi, %eax
skip_spaces:
    movb (%eax), %dl
    cmp $0, %dl
    je main_loop
    cmp $' ', %dl
    jne have_cmd
    inc %eax
    jmp skip_spaces

have_cmd:
    mov %eax, %ebx
    call split_token
    mov %eax, %ebp
    mov %edx, %ecx

    mov $cmd_pwd, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_pwd

    mov $cmd_cd, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_cd

    mov $cmd_ls, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_ls

    mov $cmd_cat, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_cat

    mov $cmd_mkdir, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_mkdir

    mov $cmd_writefile, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_writefile

    mov $cmd_run, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_run

    mov $cmd_exit, %esi
    mov %ebp, %edi
    call streq
    cmp $1, %eax
    je do_exit

    mov $1, %ebx
    mov $unk, %ecx
    mov $unk_len, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp main_loop

do_pwd:
    mov $cwd_buf, %ebx
    mov $128, %ecx
    mov $SYS_GETCWD, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je main_loop
    mov $1, %ebx
    mov $cwd_buf, %ecx
    mov %eax, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    mov $1, %ebx
    mov $nl, %ecx
    mov $1, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp main_loop

do_cd:
    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ebx
    cmpb $0, (%ebx)
    jne cd_have
    mov $slash, %ebx
cd_have:
    mov $SYS_CHDIR, %eax
    int $0x80
    jmp main_loop

do_mkdir:
    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ebx
    cmpb $0, (%ebx)
    je main_loop
    mov $SYS_MKDIR, %eax
    int $0x80
    jmp main_loop

do_ls:
    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ebx
    cmpb $0, (%ebx)
    jne ls_have
    mov $dot, %ebx
ls_have:
    xor %ecx, %ecx
    mov $SYS_OPEN, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je main_loop
    mov %eax, %esi

ls_loop:
    mov %esi, %ebx
    mov $dirbuf, %ecx
    mov $256, %edx
    mov $SYS_GETDENTS, %eax
    int $0x80
    cmp $0, %eax
    je ls_close
    cmp $0xFFFFFFFF, %eax
    je ls_close
    mov %eax, %edi
    xor %ebp, %ebp
ls_entry:
    cmp %edi, %ebp
    jae ls_loop
    mov $dirbuf, %ecx
    add %ebp, %ecx
    movzwl 8(%ecx), %eax
    lea 10(%ecx), %ecx
    push %eax
    call cstr_len
    mov %eax, %edx
    mov $1, %ebx
    mov $SYS_WRITE, %eax
    int $0x80
    mov $1, %ebx
    mov $nl, %ecx
    mov $1, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    pop %eax
    add %eax, %ebp
    jmp ls_entry

ls_close:
    mov %esi, %ebx
    mov $SYS_CLOSE, %eax
    int $0x80
    jmp main_loop

do_cat:
    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ebx
    cmpb $0, (%ebx)
    je main_loop
    xor %ecx, %ecx
    mov $SYS_OPEN, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je main_loop
    mov %eax, %esi

cat_loop:
    mov %esi, %ebx
    mov $iobuf, %ecx
    mov $256, %edx
    mov $SYS_READ, %eax
    int $0x80
    cmp $0, %eax
    je cat_close
    cmp $0xFFFFFFFF, %eax
    je cat_close
    mov $1, %ebx
    mov $iobuf, %ecx
    mov %eax, %edx
    mov $SYS_WRITE, %eax
    int $0x80
    jmp cat_loop

cat_close:
    mov %esi, %ebx
    mov $SYS_CLOSE, %eax
    int $0x80
    jmp main_loop

do_writefile:
    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ebx
    cmpb $0, (%ebx)
    je main_loop
    mov %ebx, %edi
    call split_token
    mov %eax, %ebp
    mov %edx, %ecx

    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ecx
    cmpb $0, (%ecx)
    je main_loop

    mov %ebp, %ebx
    mov $(O_CREAT|O_TRUNC), %ecx
    mov $SYS_OPEN, %eax
    int $0x80
    cmp $0xFFFFFFFF, %eax
    je main_loop
    mov %eax, %esi

    mov $1, %ebx
    mov %ecx, %edi
    call cstr_len
    mov %eax, %edx
    mov %edi, %ecx
    mov %esi, %ebx
    mov $SYS_WRITE, %eax
    int $0x80

    mov %esi, %ebx
    mov $nl, %ecx
    mov $1, %edx
    mov $SYS_WRITE, %eax
    int $0x80

    mov %esi, %ebx
    mov $SYS_CLOSE, %eax
    int $0x80
    jmp main_loop

do_run:
    mov %ecx, %eax
    call skip_spaces_ptr
    mov %eax, %ebx
    cmpb $0, (%ebx)
    je main_loop
    mov $SYS_EXECVE, %eax
    int $0x80
    jmp main_loop

do_exit:
    xor %ebx, %ebx
    mov $SYS_EXIT, %eax
    int $0x80
    jmp do_exit

split_token:
    mov %ebx, %eax
find_end:
    movb (%eax), %dl
    cmp $0, %dl
    je tok_end
    cmp $' ', %dl
    je tok_term
    inc %eax
    jmp find_end
tok_term:
    movb $0, (%eax)
    inc %eax
tok_end:
    mov %eax, %edx
    mov %ebx, %eax
    ret

skip_spaces_ptr:
    mov %eax, %edx
ss_loop:
    movb (%edx), %bl
    cmp $0, %bl
    je ss_done
    cmp $' ', %bl
    jne ss_done
    inc %edx
    jmp ss_loop
ss_done:
    mov %edx, %eax
    ret

streq:
cmp_loop:
    movb (%esi), %al
    movb (%edi), %bl
    cmp %al, %bl
    jne not_eq
    cmp $0, %al
    je eq
    inc %esi
    inc %edi
    jmp cmp_loop
eq:
    mov $1, %eax
    ret
not_eq:
    xor %eax, %eax
    ret

cstr_len:
    xor %eax, %eax
len_loop:
    cmpb $0, (%ecx,%eax,1)
    je len_done
    inc %eax
    cmp $127, %eax
    jl len_loop
len_done:
    ret

.section .rodata
banner:
    .ascii "ush: user shell\n"
banner_len = . - banner
prompt:
    .ascii "ush$ "
prompt_len = . - prompt
nl:
    .ascii "\n"
unk:
    .ascii "unknown\n"
unk_len = . - unk
cmd_pwd:
    .ascii "pwd\0"
cmd_cd:
    .ascii "cd\0"
cmd_ls:
    .ascii "ls\0"
cmd_cat:
    .ascii "cat\0"
cmd_mkdir:
    .ascii "mkdir\0"
cmd_writefile:
    .ascii "writefile\0"
cmd_run:
    .ascii "run\0"
cmd_exit:
    .ascii "exit\0"
slash:
    .ascii "/\0"
dot:
    .ascii ".\0"

.section .bss
linebuf:
    .space 256
chbuf:
    .space 1
cwd_buf:
    .space 128
dirbuf:
    .space 256
iobuf:
    .space 256
