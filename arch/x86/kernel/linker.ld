/*
 * linker.ld - Linker script for the kernel
 *
 * This script tells the linker how to layout the kernel in memory.
 * It specifies that the kernel starts at 1MB (0x100000).
 */

ENTRY(_start)

SECTIONS
{
	KERNEL_BASE = 0xC0000000;
	VMLINUX_LMA = 0x00100000;
	. = VMLINUX_LMA;

	. = ALIGN(4K);
	.text.boot :
	{
		*(.multiboot)
		*(.text.boot)
	}
	VMLINUX_LMA_OFFSET = SIZEOF(.text.boot);

	VMLINUX_VMA = KERNEL_BASE + VMLINUX_LMA + VMLINUX_LMA_OFFSET;
	. = VMLINUX_VMA;

	. = ALIGN(4K);
	.text : AT(ADDR(.text) - KERNEL_BASE)
	{
		*(.text)
		*(.text.*)
	}

	. = ALIGN(4K);
	.usertext : AT(ADDR(.usertext) - KERNEL_BASE)
	{
		*(.usertext)
	}

	/* Read-only data. */
	. = ALIGN(4K);
	.rodata : AT(ADDR(.rodata) - KERNEL_BASE)
	{
		*(.rodata)
		*(.rodata.*)
	}

	/* Linux-style initcalls */
	. = ALIGN(4K);
	.initcall.init : AT(ADDR(.initcall.init) - KERNEL_BASE)
	{
		__initcall_start = .;

		KEEP(*(.initcall0.init))
		KEEP(*(.initcall1.init))
		KEEP(*(.initcall2.init))
		KEEP(*(.initcall3.init))
		KEEP(*(.initcall4.init))
		KEEP(*(.initcall5.init))

		__initcall_end = .;
	}

	/* Read-write data (initialized) */
	. = ALIGN(4K);
	.data : AT(ADDR(.data) - KERNEL_BASE)
	{
		*(.data)
		*(.data.*)
	}

	/* Read-write data (uninitialized) and stack */
	. = ALIGN(4K);
	.bss : AT(ADDR(.bss) - KERNEL_BASE)
	{
		*(COMMON)
		*(.bss)
        *(.bootstrap_stack)
	}

    /* Define a symbol that points to the end of the kernel */
    end = .;

	/* The compiler may produce other sections, by default it will put them in
	   a segment with the same name. Simply add stuff here as needed. */
}
