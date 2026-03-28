/*
 * boot.s - Minimal kernel entry point using Multiboot
 *
 * This file defines the Multiboot header and the _start entry point.
 * It sets up a stack and calls the C kernel_main function.
 */

/* Multiboot header constants */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO  /* this is the Multiboot 'flag' field */
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */

/* Declare a header as in the Multiboot Standard. */
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/* Reserve a stack for the initial thread. */
.section .bootstrap_stack, "aw", @nobits
.align 16
stack_bottom:
.skip 32768 # 32 KiB
stack_top:

/* The kernel entry point. */
.section .text.boot
.global _start
.type _start, @function
_start:
	.set KERNEL_BASE, 0xC0000000
	/*
	 * The bootloader has loaded us into 32-bit protected mode on a x86
	 * machine. Interrupts are disabled. Paging is disabled. The processor
	 * state is as defined in the multiboot standard.
	 */

	/* Set up the stack pointer (physical). */
	mov $stack_top, %esp
	sub $KERNEL_BASE, %esp
	mov $boot_magic, %edi
	sub $KERNEL_BASE, %edi
	mov %eax, (%edi)
	mov $boot_mbi, %edi
	sub $KERNEL_BASE, %edi
	mov %ebx, (%edi)
	lgdt gdt_descriptor
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	ljmp $0x08, $1f
1:

	/* Clear page directory */
	mov $boot_page_directory, %edi
	sub $KERNEL_BASE, %edi
	xor %eax, %eax
	mov $1024, %ecx
	cld
	rep stosl

	/* Build identity map for first 4MB */
	mov $boot_page_table_low, %edi
	sub $KERNEL_BASE, %edi
	xor %eax, %eax
	mov $1024, %ecx
	rep stosl
	mov $boot_page_table_low, %edi
	sub $KERNEL_BASE, %edi
	xor %eax, %eax
	mov $1024, %ecx
1:
	mov %eax, %edx
	or $0x3, %edx
	mov %edx, (%edi)
	add $0x1000, %eax
	add $4, %edi
	loop 1b

	/* Build higher-half mapping for first 128MB */
	mov $boot_page_tables, %edi
	sub $KERNEL_BASE, %edi
	xor %eax, %eax
	mov $(32*1024), %ecx
2:
	mov %eax, %edx
	or $0x3, %edx
	mov %edx, (%edi)
	add $0x1000, %eax
	add $4, %edi
	loop 2b

	/* Map PDE[0] = identity */
	mov $boot_page_directory, %edi
	sub $KERNEL_BASE, %edi
	mov $boot_page_table_low, %eax
	sub $KERNEL_BASE, %eax
	or $0x3, %eax
	mov %eax, (%edi)

	/* Map PDE[768..799] = higher-half 128MB */
	mov $boot_page_tables, %eax
	sub $KERNEL_BASE, %eax
	mov $0, %ecx
3:
	mov %eax, %edx
	or $0x3, %edx
	mov %edx, 768*4(%edi,%ecx,4)
	add $4096, %eax
	inc %ecx
	cmp $32, %ecx
	jl 3b

	/* Enable paging */
	mov $boot_page_directory, %eax
	sub $KERNEL_BASE, %eax
	mov %eax, %cr3
	mov %cr0, %eax
	or $0x80000000, %eax
	mov %eax, %cr0

	mov $trampoline_high, %eax
	add $KERNEL_BASE, %eax
	jmp *%eax

trampoline_high:
	mov $stack_top, %esp
	mov $boot_page_directory, %edi
	movl $0, (%edi)
	mov $boot_page_directory, %eax
	sub $KERNEL_BASE, %eax
	mov %eax, %cr3

	/* Push Multiboot information structure pointer (ebx) and magic number (eax) */
	mov boot_mbi, %ebx
	add $KERNEL_BASE, %ebx
	mov boot_magic, %eax
	push %ebx
	push %eax

	/*
	 * Call the global constructors. (Optional for C, but good practice if
	 * you ever add C++ or advanced C features).
	 */

	/* 跳转到 C 语言的内核入口函数 */
	mov $kernel_entry, %eax
	call *%eax

	/*
	 * If the system has nothing more to do, put the computer into an
	 * infinite loop. To do that:
	 * 1. Disable interrupts with cli (clear interrupt enable in eflags).
	 * 2. Wait for the next interrupt with hlt (panic instruction).
	 * 3. Jump to the hlt instruction if it ever wakes up.
	 */
	cli
1:	hlt
	jmp 1b

/*
 * Set the size of the _start symbol to the current location '.' minus its start.
 * This is useful when debugging or when you implement call tracing.
 */
.size _start, . - _start

.section .bss
.align 4096
.global boot_page_directory
boot_page_directory:
	.skip 4096
.align 4096
boot_page_table_low:
	.skip 4096
.align 4096
boot_page_tables:
	.skip 4096*32
boot_magic:
	.long 0
boot_mbi:
	.long 0

.section .text.boot
.align 8
gdt_start:
	.quad 0x0000000000000000
	.quad 0x00CF9A000000FFFF
	.quad 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
	.word gdt_end - gdt_start - 1
	.long gdt_start
