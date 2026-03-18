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
#include "task.h"
#include "syscall.h"
#include "procfs.h"
#include "devfs.h"
#include "sysfs.h"
#include "fs.h"
#include "vfs.h"
#include "ramfs.h"

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
   outb(0x3f8 + 1, 0x00); // Disable all interrupts
   outb(0x3f8 + 3, 0x80); // Enable DLAB (set baud rate divisor)
   outb(0x3f8 + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
   outb(0x3f8 + 1, 0x00); // (hi byte)
   outb(0x3f8 + 3, 0x03); // 8 bits, no parity, one stop bit
   outb(0x3f8 + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
   outb(0x3f8 + 4, 0x0B); // IRQs enabled, RTS/DSR set
   outb(0x3f8 + 1, 0x01); // Enable Received Data Available Interrupt
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

static void task_demo_a(void)
{
    for (;;) {
        if (task_get_demo_enabled()) {
            terminal_putchar('A');
        }
        task_sleep(5);
        task_yield();
    }
}

static void task_demo_b(void)
{
    for (;;) {
        if (task_get_demo_enabled()) {
            terminal_putchar('B');
        }
        task_sleep(5);
        task_yield();
    }
}

static void enter_user_mode(uint32_t entry, uint32_t user_stack)
{
    __asm__ volatile(
        "cli\n"
        "mov $0x23, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushl $0x23\n"
        "pushl %[stack]\n"
        "pushf\n"
        "popl %%eax\n"
        "orl $0x200, %%eax\n"
        "pushl %%eax\n"
        "pushl $0x1B\n"
        "pushl %[entry]\n"
        "iret\n"
        :
        : [entry] "r"(entry), [stack] "r"(user_stack)
        : "eax", "memory"
    );
}

typedef struct __attribute__((packed)) {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

static uint32_t align_down(uint32_t value)
{
    return value & ~0xFFF;
}

static uint32_t align_up(uint32_t value)
{
    return (value + 0xFFF) & ~0xFFF;
}

static void ensure_private_table(uint32_t* dir, uint32_t pde_idx)
{
    if (dir[pde_idx] & PTE_PRESENT) {
        uint32_t* old_table = (uint32_t*)(dir[pde_idx] & ~0xFFF);
        uint32_t* new_table = (uint32_t*)pmm_alloc_page();
        if (!new_table) {
            return;
        }
        memcpy(new_table, old_table, 4096);
        dir[pde_idx] = ((uint32_t)new_table) | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    } else {
        uint32_t* new_table = (uint32_t*)pmm_alloc_page();
        if (!new_table) {
            return;
        }
        memset(new_table, 0, 4096);
        dir[pde_idx] = ((uint32_t)new_table) | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    }
}

static void ensure_private_table_range(uint32_t* dir, uint32_t start, uint32_t end)
{
    uint32_t start_idx = start / (1024 * 4096);
    uint32_t end_idx = (end - 1) / (1024 * 4096);
    for (uint32_t i = start_idx; i <= end_idx; i++) {
        ensure_private_table(dir, i);
    }
}

static int load_user_program(const char* name, uint32_t* entry, uint32_t* user_stack, uint32_t** out_dir,
                             uint32_t* out_base, uint32_t* out_pages, uint32_t* out_stack_base)
{
    if (!fs_root) {
        terminal_writestring("No file system mounted.\n");
        return -1;
    }

    fs_node_t *node = vfs_resolve(name);
    if (!node) {
        terminal_writestring("User program not found.\n");
        return -1;
    }

    if (node->length == 0) {
        terminal_writestring("User program is empty.\n");
        return -1;
    }

    uint8_t *buf = (uint8_t*)kmalloc(node->length);
    if (!buf) {
        terminal_writestring("User program buffer alloc failed.\n");
        return -1;
    }
    uint32_t read = read_fs(node, 0, node->length, buf);
    if (read != node->length) {
        terminal_writestring("User program read failed.\n");
        kfree(buf);
        return -1;
    }

    if (node->length < sizeof(Elf32_Ehdr)) {
        terminal_writestring("User program too small.\n");
        kfree(buf);
        return -1;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr*)buf;
    if (!(ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' &&
          ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F')) {
        terminal_writestring("User program is not ELF.\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_ident[4] != 1 || ehdr->e_ident[5] != 1) {
        terminal_writestring("User program ELF format unsupported.\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_ehsize != sizeof(Elf32_Ehdr)) {
        terminal_writestring("User program ELF header size mismatch.\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_phentsize != sizeof(Elf32_Phdr)) {
        terminal_writestring("User program ELF phdr size mismatch.\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_type != 2 || ehdr->e_machine != 3 || ehdr->e_version != 1) {
        terminal_writestring("User program ELF header invalid.\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_phoff + (uint32_t)ehdr->e_phnum * ehdr->e_phentsize > node->length) {
        terminal_writestring("User program header out of range.\n");
        kfree(buf);
        return -1;
    }

    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1 || phdr->p_memsz == 0) {
            continue;
        }
        if (phdr->p_vaddr >= 0xC0000000 || (phdr->p_vaddr + phdr->p_memsz) >= 0xC0000000) {
            terminal_writestring("User program vaddr out of range.\n");
            kfree(buf);
            return -1;
        }
        if (phdr->p_filesz > 0 && phdr->p_offset + phdr->p_filesz > node->length) {
            terminal_writestring("User program segment out of range.\n");
            kfree(buf);
            return -1;
        }
        if (phdr->p_vaddr < min_vaddr) min_vaddr = phdr->p_vaddr;
        if (phdr->p_vaddr + phdr->p_memsz > max_vaddr) max_vaddr = phdr->p_vaddr + phdr->p_memsz;
    }

    if (min_vaddr == 0xFFFFFFFF) {
        terminal_writestring("User program has no loadable segments.\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_entry < min_vaddr || ehdr->e_entry >= max_vaddr) {
        terminal_writestring("User program entry out of range.\n");
        kfree(buf);
        return -1;
    }

    enum { PF_X = 1, PF_W = 2, PF_R = 4 };

    uint32_t* user_dir = vmm_clone_kernel_directory();
    if (!user_dir) {
        terminal_writestring("User page directory alloc failed.\n");
        kfree(buf);
        return -1;
    }

    uint32_t user_base = align_down(min_vaddr);
    uint32_t user_end = align_up(max_vaddr);
    uint32_t user_stack_base = 0xBFFFF000;
    uint32_t pages = (user_end - user_base) / 4096;
    uint32_t heap_base = user_end;

    ensure_private_table(user_dir, user_stack_base / (1024 * 4096));

    task_user_vmas_reset();
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1 || phdr->p_memsz == 0) {
            continue;
        }
        uint32_t seg_start = align_down(phdr->p_vaddr);
        uint32_t seg_end = align_up(phdr->p_vaddr + phdr->p_memsz);
        uint32_t vma_flags = VMA_READ;
        if (phdr->p_flags & PF_W) vma_flags |= VMA_WRITE;
        if (phdr->p_flags & PF_X) vma_flags |= VMA_EXEC;
        task_user_vma_add(seg_start, seg_end, vma_flags);

        ensure_private_table_range(user_dir, seg_start, seg_end);
        for (uint32_t va = seg_start; va < seg_end; va += 4096) {
            uint32_t old_phys = vmm_virt_to_phys_ex(user_dir, (void*)va);
            if (old_phys != 0xFFFFFFFF && ((old_phys & ~0xFFF) != va)) {
                continue;
            }
            void *phys = pmm_alloc_page();
            if (!phys) {
                terminal_writestring("User program page alloc failed.\n");
                kfree(buf);
                return -1;
            }
            vmm_map_page_ex(user_dir, phys, (void*)va, PTE_PRESENT | PTE_READ_WRITE | PTE_USER);
        }
    }
    task_user_vma_add(user_stack_base, user_stack_base + 4096, VMA_READ | VMA_WRITE);
    task_user_heap_init(heap_base, user_stack_base);

    void *stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        terminal_writestring("User stack alloc failed.\n");
        kfree(buf);
        return -1;
    }
    vmm_map_page_ex(user_dir, stack_phys, (void*)user_stack_base,
                    PTE_PRESENT | PTE_READ_WRITE | PTE_USER);

    uint32_t entry_point = ehdr->e_entry;
    uint32_t* old_dir = vmm_get_current_directory();
    vmm_switch_directory(user_dir);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1 || phdr->p_memsz == 0) {
            continue;
        }
        memset((void*)phdr->p_vaddr, 0, phdr->p_memsz);
        if (phdr->p_filesz > 0) {
            memcpy((void*)phdr->p_vaddr, buf + phdr->p_offset, phdr->p_filesz);
        }
        if (!(phdr->p_flags & PF_W)) {
            uint32_t seg_start = align_down(phdr->p_vaddr);
            uint32_t seg_end = align_up(phdr->p_vaddr + phdr->p_memsz);
            for (uint32_t va = seg_start; va < seg_end; va += 4096) {
                vmm_set_page_readonly_ex(user_dir, (void*)va);
            }
        }
    }
    vmm_switch_directory(old_dir);
    kfree(buf);

    *entry = entry_point;
    *user_stack = 0xC0000000;
    *out_dir = user_dir;
    *out_base = user_base;
    *out_pages = pages;
    *out_stack_base = user_stack_base;
    return 0;
}

void user_task(void)
{
    uint32_t entry = 0;
    uint32_t stack = 0;
    uint32_t* user_dir = NULL;
    uint32_t user_base = 0;
    uint32_t user_pages = 0;
    uint32_t user_stack_base = 0;
    const char *program = task_get_current_program();
    if (!program) program = "user.elf";
    if (load_user_program(program, &entry, &stack, &user_dir,
                          &user_base, &user_pages, &user_stack_base) != 0) {
        task_exit_with_status(1);
        return;
    }

    task_set_current_page_directory(user_dir);
    task_set_user_info(user_base, user_pages, user_stack_base);
    vmm_switch_directory(user_dir);
    enter_user_mode(entry, stack);
    for(;;);
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

void user_mode_launch(void)
{
    shell_set_foreground(1);
    task_create(user_task);
    terminal_writestring("User task created.\n");
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
                    fs_node_t *proc_root = procfs_init();
                    fs_node_t *dev_root = devfs_init();
                    fs_node_t *sys_root = sysfs_init();
                    fs_node_t *ram_root = ramfs_init();
                    vfs_init();
                    vfs_mount_root("/", ram_root);
                    vfs_mount_root("/initrd", fs_root);
                    vfs_mount_root("/proc", proc_root);
                    vfs_mount_root("/dev", dev_root);
                    vfs_mount_root("/sys", sys_root);
                    vfs_chdir("/initrd");
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
    syscall_init();

    /* Initialize Interrupt Service Routines (PIC remap + IRQ handlers) */
    irq_install();
    serial_write("Interrupts initialized.\n");

    /* Initialize Keyboard Driver */
    init_keyboard();

    /* Initialize PIT Timer (100 Hz = 10ms per tick) */
    init_timer(100);

    /* Initialize basic tasking */
    tasking_init();
    task_create(task_demo_a);
    task_create(task_demo_b);

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
    task_create(shell_task);

    /* Infinite loop to keep the kernel running and responsive to interrupts */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
