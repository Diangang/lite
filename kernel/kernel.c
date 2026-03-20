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
#include "device_model.h"
#include "tty.h"
#include "serial.h"
#include "vga.h"
#include "console.h"

static void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv) return;
    memset(drv, 0, sizeof(*drv));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(drv->kobj.name)) n = sizeof(drv->kobj.name) - 1;
        memcpy(drv->kobj.name, name, n);
        drv->kobj.name[n] = 0;
    }
    drv->kobj.refcount = 1;
    drv->bus = bus;
    drv->probe = probe;
}

static int probe_nop(struct device *dev)
{
    (void)dev;
    return 0;
}

static void task_demo_a(void)
{
    for (;;) {
        if (task_get_demo_enabled()) {
            console_put_char('A');
        }
        task_sleep(5);
        task_yield();
    }
}

static void task_demo_b(void)
{
    for (;;) {
        if (task_get_demo_enabled()) {
            console_put_char('B');
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
    if (!fs_root)
        return printf("No file system mounted.\n"), -1;

    struct fs_node *node = vfs_resolve(name);
    if (!node)
        return printf("User program not found.\n"), -1;

    if (node->length == 0)
        return printf("User program is empty.\n"), -1;

    uint8_t *buf = (uint8_t*)kmalloc(node->length);
    if (!buf)
        return printf("User program buffer alloc failed.\n"), -1;

    uint32_t read = read_fs(node, 0, node->length, buf);
    if (read != node->length) {
        printf("User program read failed.\n");
        kfree(buf);
        return -1;
    }

    if (node->length < sizeof(Elf32_Ehdr))
        return printf("User program too small.\n"), kfree(buf), -1;

    Elf32_Ehdr *ehdr = (Elf32_Ehdr*)buf;
    if (!(ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' &&
          ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F'))
        return printf("User program is not ELF.\n"), kfree(buf), -1;

    if (ehdr->e_ident[4] != 1 || ehdr->e_ident[5] != 1)
        return printf("User program ELF format unsupported.\n"), kfree(buf), -1;

    if (ehdr->e_ehsize != sizeof(Elf32_Ehdr))
        return printf("User program ELF header size mismatch.\n"), kfree(buf), -1;

    if (ehdr->e_phentsize != sizeof(Elf32_Phdr))
        return printf("User program ELF phdr size mismatch.\n"), kfree(buf), -1;

    if (ehdr->e_type != 2 || ehdr->e_machine != 3 || ehdr->e_version != 1)
        return printf("User program ELF header invalid.\n"), kfree(buf), -1;

    if (ehdr->e_phoff + (uint32_t)ehdr->e_phnum * ehdr->e_phentsize > node->length)
        return printf("User program header out of range.\n"), kfree(buf), -1;

    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1) // PT_LOAD
            continue;
        if (phdr->p_vaddr >= 0xC0000000 || (phdr->p_vaddr + phdr->p_memsz) >= 0xC0000000)
            return printf("User program vaddr out of range.\n"), kfree(buf), -1;

        if (phdr->p_filesz > 0 && phdr->p_offset + phdr->p_filesz > node->length)
            return printf("User program segment out of range.\n"), kfree(buf), -1;

        if (phdr->p_vaddr < min_vaddr) min_vaddr = phdr->p_vaddr;
        if (phdr->p_vaddr + phdr->p_memsz > max_vaddr) max_vaddr = phdr->p_vaddr + phdr->p_memsz;
    }

    if (min_vaddr == 0xFFFFFFFF)
        return printf("User program has no loadable segments.\n"), kfree(buf), -1;

    if (ehdr->e_entry < min_vaddr || ehdr->e_entry >= max_vaddr)
        return printf("User program entry out of range.\n"), kfree(buf), -1;

    enum { PF_X = 1, PF_W = 2, PF_R = 4 };

    uint32_t* user_dir = vmm_clone_kernel_directory();
    if (!user_dir)
        return printf("User page directory alloc failed.\n"), kfree(buf), -1;

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
            if (!phys)
                return printf("User program page alloc failed.\n"), kfree(buf), -1;
            vmm_map_page_ex(user_dir, phys, (void*)va, PTE_PRESENT | PTE_READ_WRITE | PTE_USER);
        }
    }
    task_user_vma_add(user_stack_base, user_stack_base + 4096, VMA_READ | VMA_WRITE);
    task_user_heap_init(heap_base, user_stack_base);

    void *stack_phys = pmm_alloc_page();
    if (!stack_phys)
        return printf("User stack alloc failed.\n"), kfree(buf), -1;
    vmm_map_page_ex(user_dir, stack_phys, (void*)user_stack_base,
                    PTE_PRESENT | PTE_READ_WRITE | PTE_USER);

    uint32_t entry_point = ehdr->e_entry;
    uint32_t* old_dir = vmm_get_current_directory();
    vmm_switch_directory(user_dir);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1 || phdr->p_memsz == 0)
            continue;
        memset((void*)phdr->p_vaddr, 0, phdr->p_memsz);
        if (phdr->p_filesz > 0)
            memcpy((void*)phdr->p_vaddr, buf + phdr->p_offset, phdr->p_filesz);
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

int kernel_load_user_program(const char* name, uint32_t* entry, uint32_t* user_stack, uint32_t** out_dir,
                             uint32_t* out_base, uint32_t* out_pages, uint32_t* out_stack_base)
{
    return load_user_program(name, entry, user_stack, out_dir, out_base, out_pages, out_stack_base);
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
    panic("Returned from user mode?!");
}

void user_mode_launch(void)
{
    shell_set_foreground(1);
    task_create(user_task);
    printf("User task created.\n");
}

void kernel_main(struct multiboot_info* mbi, uint32_t magic)
{
    /* Initialize serial port FIRST so we can debug without VGA */
    serial_init();
    console_set_targets(CONSOLE_TARGET_SERIAL);
    printf("Serial initialized.\n");

    /* Initialize terminal vga interface */
    vga_initialize();
    console_set_targets(CONSOLE_TARGET_VGA);
    printf("VGA initialized.\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Invalid Multiboot magic number!");

    /* Initialize Ramdisk if modules are loaded */
    if (mbi->mods_addr == 0)
        panic("No Multiboot modules found! InitRD not loaded.");

    /* Initialize Global Descriptor Table */
    init_gdt();
    printf("GDT initialized.\n");

    /* Initialize Physical Memory Manager */
    pmm_init(mbi);
    printf("PMM initialized.\n");

    /* Initialize Virtual Memory Manager */
    vmm_init();
    printf("VMM initialized.\n");

    /* Initialize Kernel Heap */
    kheap_init();
    printf("KHEAP initialized.\n");

    struct multiboot_module* mod = (struct multiboot_module*)mbi->mods_addr;
    uint32_t initrd_location = mod->mod_start;
    uint32_t initrd_end = mod->mod_end;

    if (initrd_location == 0xFFFFFFFF || initrd_location == 0)
        panic("ERROR: Invalid InitRD location!");

    fs_root = init_initrd(initrd_location);
    if (!fs_root)
        panic("Failed to load Ramdisk.");

    struct fs_node *proc_root = procfs_init();
    struct fs_node *dev_root = devfs_init();
    device_model_init();
    struct bus_type *platform = device_model_platform_bus();
    if (platform) {
        device_register_simple("console", "console", platform, devfs_get_console());
        device_register_simple("initrd", "initrd", platform, fs_root);
    }
    struct fs_node *sys_root = sysfs_init();
    struct fs_node *ram_root = ramfs_init();
    if (platform) {
        device_register_simple("ramfs", "memfs", platform, ram_root);
        static struct device_driver drv_console;
        static struct device_driver drv_initrd;
        static struct device_driver drv_memfs;
        init_driver(&drv_console, "console", platform, probe_nop);
        init_driver(&drv_initrd, "initrd", platform, probe_nop);
        init_driver(&drv_memfs, "memfs", platform, probe_nop);
        driver_register(&drv_console);
        driver_register(&drv_initrd);
        driver_register(&drv_memfs);
    }
    vfs_init();
    vfs_mount_root("/", ram_root);
    vfs_mount_root("/initrd", fs_root);
    vfs_mount_root("/proc", proc_root);
    vfs_mount_root("/dev", dev_root);
    vfs_mount_root("/sys", sys_root);
    vfs_chdir("/initrd");
    printf("Ramdisk loaded.\n");

    /* Initialize Interrupt Descriptor Table */
    init_idt();
    printf("IDT initialized.\n");

    /* Install CPU Exceptions before IRQs! */
    isr_install();
    syscall_init();

    /* Initialize Interrupt Service Routines (PIC remap + IRQ handlers) */
    irq_install();
    printf("Interrupts initialized.\n");

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
	printf("Hello, Kernel World!\n");
	printf("This is a minimal kernel running on QEMU.\n");
	printf("Memory address 0xB8000 is directly manipulated.\n");
	printf("Enjoy your OS development journey!\n");

    /* Initialize the shell */
    shell_init();
    int init_pid = task_create_user("init.elf");
    if (init_pid < 0)
        shell_set_foreground(0), printf("init.elf not found, staying in kernel shell.\n");
    else
        shell_set_foreground_pid((uint32_t)init_pid), printf("init task created.\n");
    task_create(shell_task);

    /* Infinite loop to keep the kernel running and responsive to interrupts */
    panic(NULL);
}

__attribute__((section(".text.entry")))
void kernel_entry(uint32_t magic, struct multiboot_info* mbi)
{
    outb(0xE9, 'K');
    kernel_main(mbi, magic);
    panic(NULL);
}
