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
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

/* The kernel entry point. */
.section .text
.global _start
.type _start, @function
_start:
	/*
	 * The bootloader has loaded us into 32-bit protected mode on a x86
	 * machine. Interrupts are disabled. Paging is disabled. The processor
	 * state is as defined in the multiboot standard.
	 */

	/* Set up the stack pointer. */
	mov $stack_top, %esp

	/*
	 * Call the global constructors. (Optional for C, but good practice if
	 * you ever add C++ or advanced C features).
	 */

	/* Transfer control to the main kernel. */
	call kernel_main

	/*
	 * If the system has nothing more to do, put the computer into an
	 * infinite loop. To do that:
	 * 1. Disable interrupts with cli (clear interrupt enable in eflags).
	 * 2. Wait for the next interrupt with hlt (halt instruction).
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
