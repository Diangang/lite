#include "kernel.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "libc.h"

/* Check if the compiler thinks we are targeting the wrong operating system. */

void kernel_main(void);

__attribute__((section(".text.entry")))
void kernel_entry(void)
{
	outb(0xE9, 'K');
	kernel_main();
	for (;;) {
		__asm__ volatile ("hlt");
	}
}

enum {
	COM1 = 0x3F8
};

static void serial_init(void)
{
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x80);
	outb(COM1 + 0, 0x03);
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);
	outb(COM1 + 2, 0xC7);
	outb(COM1 + 4, 0x0B);
}

static int serial_is_transmit_empty(void)
{
	return inb(COM1 + 5) & 0x20;
}

static void serial_putchar(char c)
{
	while (!serial_is_transmit_empty()) {
	}
	outb(COM1, (uint8_t)c);
}

static void serial_write(const char* data)
{
	for (size_t i = 0; data[i]; i++)
		serial_putchar(data[i]);
}

/* Check if the compiler thinks we are targeting the wrong operating system. */
/* #if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif */

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
/* #error "This tutorial needs to be compiled with a ix86-elf compiler" */
#endif

/* Hardware text mode color constants. */
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg)
{
	return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color)
{
	return (uint16_t) uc | (uint16_t) color << 8;
}

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;

void terminal_initialize(void)
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	terminal_buffer = (uint16_t*) 0xB8000;
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
}

void terminal_setcolor(uint8_t color)
{
	terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

static void terminal_scroll(void)
{
    /* Move all lines up by one */
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            size_t from_index = y * VGA_WIDTH + x;
            size_t to_index = (y - 1) * VGA_WIDTH + x;
            terminal_buffer[to_index] = terminal_buffer[from_index];
        }
    }
    /* Clear the last line */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = vga_entry(' ', terminal_color);
    }
    terminal_row = VGA_HEIGHT - 1;
}

void terminal_putchar(char c)
{
    if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
        return;
    }
	if (c == '\n') {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT)
			terminal_scroll();
		return;
	}
	terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT)
			terminal_scroll();
	}
}

void terminal_write(const char* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data)
{
	terminal_write(data, strlen(data));
}

void kernel_main(void)
{
	/* Initialize terminal interface */
	terminal_initialize();

    /* Initialize serial port */
    serial_init();

    /* Initialize Global Descriptor Table */
    init_gdt();
    terminal_writestring("GDT initialized.\n");

    /* Initialize Interrupt Descriptor Table */
    init_idt();
    terminal_writestring("IDT initialized.\n");

    /* Install CPU Exceptions before IRQs! */
    isr_install();
    terminal_writestring("CPU Exceptions installed.\n");

    /* Initialize Interrupt Service Routines (PIC remap + IRQ handlers) */
    irq_install();
    terminal_writestring("ISRs and PIC initialized.\n");

    /* Initialize Keyboard Driver */
    init_keyboard();
    terminal_writestring("Keyboard initialized. Try typing!\n");

    /* Initialize PIT Timer (100 Hz = 10ms per tick) */
    init_timer(100);
    terminal_writestring("Timer initialized (100 Hz).\n");

    /* Enable Interrupts */
    __asm__ volatile ("sti");
    terminal_writestring("Interrupts enabled (STI).\n");

	/* Newline support is rudimentary in this example */
	terminal_writestring("Hello, Kernel World!\n");
	terminal_writestring("This is a minimal kernel running on QEMU.\n");
	terminal_writestring("Memory address 0xB8000 is directly manipulated.\n");
	terminal_writestring("Enjoy your OS development journey!\n");

	serial_write("Hello, Kernel World!\n");
	serial_write("This is a minimal kernel running on QEMU.\n");
	serial_write("Memory address 0xB8000 is directly manipulated.\n");
	serial_write("Enjoy your OS development journey!\n");

    /* Initialize the shell */
    shell_init();

    /* Infinite loop to keep the kernel running and responsive to interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
