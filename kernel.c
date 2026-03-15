#include "kernel.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "libc.h"
#include "multiboot.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "initrd.h"

/* Check if the compiler thinks we are targeting the wrong operating system. */

void kernel_main(multiboot_info_t* mbi, uint32_t magic);

__attribute__((section(".text.entry")))
void kernel_entry(uint32_t magic, multiboot_info_t* mbi)
{
	outb(0xE9, 'K');
	kernel_main(mbi, magic);
	for (;;) {
		__asm__ volatile ("hlt");
	}
}

/* Serial Helper Functions */
void serial_init() {
   outb(0x3f8 + 1, 0x00);    // Disable all interrupts
   outb(0x3f8 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(0x3f8 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(0x3f8 + 1, 0x00);    //                  (hi byte)
   outb(0x3f8 + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(0x3f8 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(0x3f8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   outb(0x3f8 + 1, 0x01);    // Enable Received Data Available Interrupt
}

int is_transmit_empty() {
   return inb(0x3f8 + 5) & 0x20;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);
   outb(0x3f8, a);
}

void serial_write(const char* data) {
    while (*data) {
        write_serial(*data++);
    }
}

void serial_handler(registers_t *regs) {
    (void)regs;
    /* Check if it's a read interrupt (IIR) */
    /* Read the character */
    if (inb(0x3f8 + 5) & 1) {
        char c = inb(0x3f8);
        /* Echo back is handled by shell */
        /* Pass to shell */
        shell_process_char(c);
    }
}

void serial_write_hex(uint32_t n) {
    serial_write("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t digit = (n >> i) & 0xF;
        char c = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        write_serial(c);
    }
    serial_write("\n");
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
    /* Output to Serial Port as well */
    write_serial(c);

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

void kernel_main(multiboot_info_t* mbi, uint32_t magic)
{
    /* Initialize serial port FIRST so we can debug without VGA */
    serial_init();
    serial_write("Serial initialized.\n");

    /* Initialize terminal interface */
    terminal_initialize();

    /* Initialize Global Descriptor Table */
    init_gdt();
    serial_write("GDT initialized.\n");

    /* Initialize Physical Memory Manager */
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        pmm_init(mbi);
        serial_write("PMM initialized.\n");

        /* Initialize Virtual Memory Manager */
        vmm_init();
        serial_write("VMM initialized.\n");

        /* Initialize Kernel Heap */
        kheap_init();
        serial_write("KHEAP initialized.\n");

        /* Initialize Ramdisk if modules are loaded */
        if (mbi->mods_count > 0) {
            multiboot_module_t* mod = (multiboot_module_t*)mbi->mods_addr;
            uint32_t initrd_location = mod->mod_start;
            uint32_t initrd_end = mod->mod_end;

            if (initrd_location == 0xFFFFFFFF || initrd_location == 0) {
                serial_write("ERROR: Invalid InitRD location!\n");
                terminal_writestring("ERROR: Invalid InitRD location!\n");
            } else {
                /* Identity map the initrd range to be safe */
                uint32_t start_page = initrd_location & 0xFFFFF000;
                uint32_t end_page = (initrd_end & 0xFFFFF000) + 4096;
                // Avoid potential overflow if end_page is near 4GB
                if (end_page < start_page) end_page = 0xFFFFF000;

                for (uint32_t addr = start_page; addr < end_page; addr += 4096) {
                     vmm_map_page((void*)addr, (void*)addr);
                }

                fs_root = init_initrd(initrd_location);
                if (fs_root) {
                    serial_write("Ramdisk loaded.\n");
                    terminal_writestring("Ramdisk loaded.\n");
                } else {
                    serial_write("Failed to load Ramdisk.\n");
                    terminal_writestring("Failed to load Ramdisk.\n");
                }
            }
        } else {
            terminal_writestring("WARNING: No Multiboot modules found! InitRD not loaded.\n");
        }
    } else {
        terminal_writestring("Invalid Multiboot magic number!\n");
    }

    /* Initialize Interrupt Descriptor Table */
    init_idt();
    serial_write("IDT initialized.\n");

    /* Install CPU Exceptions before IRQs! */
    isr_install();

    /* Initialize Interrupt Service Routines (PIC remap + IRQ handlers) */
    irq_install();
    serial_write("Interrupts initialized.\n");

    /* Initialize Keyboard Driver */
    init_keyboard();

    /* Initialize PIT Timer (100 Hz = 10ms per tick) */
    init_timer(100);

    /* Enable Interrupts */
    __asm__ volatile ("sti");

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
