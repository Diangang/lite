#include "task_internal.h"
#include "file.h"
#include "kheap.h"
#include "libc.h"
#include "timer.h"
#include "vmm.h"
#include "pmm.h"
#include "syscall.h"
#include "gdt.h"
#include "devfs.h"
#include "fs.h"
#include "console.h"

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
        if (!new_table)
            return;
        memcpy(new_table, old_table, 4096);
        dir[pde_idx] = ((uint32_t)new_table) | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    } else {
        uint32_t* new_table = (uint32_t*)pmm_alloc_page();
        if (!new_table)
            return;
        memset(new_table, 0, 4096);
        dir[pde_idx] = ((uint32_t)new_table) | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    }
}

static void ensure_private_table_range(uint32_t* dir, uint32_t start, uint32_t end)
{
    uint32_t start_idx = start / (1024 * 4096);
    uint32_t end_idx = (end - 1) / (1024 * 4096);
    for (uint32_t i = start_idx; i <= end_idx; i++)
        ensure_private_table(dir, i);
}

int kernel_load_user_program(const char* name, uint32_t* entry, uint32_t* user_stack, uint32_t** out_dir,
                             uint32_t* out_base, uint32_t* out_pages, uint32_t* out_stack_base)
{
    struct inode *node = vfs_resolve(name);
    if (!node) {
        char try_path[128];
        strcpy(try_path, "/bin/");
        const char *basename = name;
        const char *p = name;
        while (*p) {
            if (*p == '/') basename = p + 1;
            p++;
        }
        strcpy(try_path + 5, basename);
        node = vfs_resolve(try_path);
        if (!node) {
            node = vfs_resolve(basename);
            if (!node) {
                strcpy(try_path, "/initrd/");
                strcpy(try_path + 8, basename);
                node = vfs_resolve(try_path);
                if (!node)
                    return printf("User program not found: %s\n", name), -1;
            }
        }
    }

    if (node->i_size == 0)
        return printf("User program is empty.\n"), -1;

    uint8_t *buf = (uint8_t*)kmalloc(node->i_size);
    if (!buf)
        return printf("User program buffer alloc failed.\n"), -1;

    uint32_t read = read_fs(node, 0, node->i_size, buf);
    if (read != node->i_size) {
        printf("User program read failed.\n");
        kfree(buf);
        return -1;
    }

    if (node->i_size < sizeof(Elf32_Ehdr))
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

    if (ehdr->e_phoff + (uint32_t)ehdr->e_phnum * ehdr->e_phentsize > node->i_size)
        return printf("User program header out of range.\n"), kfree(buf), -1;

    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1)
            continue;
        if (phdr->p_vaddr >= 0xC0000000 || (phdr->p_vaddr + phdr->p_memsz) >= 0xC0000000)
            return printf("User program vaddr out of range.\n"), kfree(buf), -1;

        if (phdr->p_filesz > 0 && phdr->p_offset + phdr->p_filesz > node->i_size)
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
        if (phdr->p_type != 1 || phdr->p_memsz == 0)
            continue;
        uint32_t seg_start = align_down(phdr->p_vaddr);
        uint32_t seg_end = align_up(phdr->p_vaddr + phdr->p_memsz);
        uint32_t vma_flags = VMA_READ;
        if (phdr->p_flags & PF_W) vma_flags |= VMA_WRITE;
        if (phdr->p_flags & PF_X) vma_flags |= VMA_EXEC;
        task_user_vma_add(seg_start, seg_end, vma_flags);

        ensure_private_table_range(user_dir, seg_start, seg_end);
        for (uint32_t va = seg_start; va < seg_end; va += 4096) {
            uint32_t old_phys = vmm_virt_to_phys_ex(user_dir, (void*)va);
            if (old_phys != 0xFFFFFFFF && ((old_phys & ~0xFFF) != va))
                continue;
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
            for (uint32_t va = seg_start; va < seg_end; va += 4096)
                vmm_set_page_readonly_ex(user_dir, (void*)va);
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
    const char *comm = task_get_current_comm();
    if (!comm) comm = "user.elf";
    if (kernel_load_user_program(comm, &entry, &stack, &user_dir,
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

int task_exec_user(const char *program)
{
    if (!program)
        return -1;
    if (!current)
        return -1;

    task_fdtable_init(current);
    task_install_stdio(devfs_get_console());

    if (!current->mm) {
        current->mm = mm_create();
        if (!current->mm)
            return -1;
    }

    task_set_comm(current, program);

    uint32_t entry = 0;
    uint32_t stack = 0;
    uint32_t* user_dir = NULL;
    uint32_t user_base = 0;
    uint32_t user_pages = 0;
    uint32_t user_stack_base = 0;
    if (kernel_load_user_program(program, &entry, &stack, &user_dir,
                          &user_base, &user_pages, &user_stack_base) != 0)
        return -1;

    task_set_current_page_directory(user_dir);
    task_set_user_info(user_base, user_pages, user_stack_base);
    vmm_switch_directory(user_dir);
    enter_user_mode(entry, stack);
    panic("Returned from user mode?!");
    return -1;
}

int task_execve(const char *program, struct pt_regs *regs)
{
    if (!program || !*program || !regs)
        return -1;
    if (!current)
        return -1;
    if (!current->mm)
        return -1;

    struct mm_struct *old_mm = current->mm;
    struct mm_struct *new_mm = mm_create();
    if (!new_mm)
        return -1;
    current->mm = new_mm;

    uint32_t entry = 0;
    uint32_t user_stack = 0;
    uint32_t *user_dir = NULL;
    uint32_t user_base = 0;
    uint32_t user_pages = 0;
    uint32_t user_stack_base = 0;
    if (kernel_load_user_program(program, &entry, &user_stack, &user_dir,
                                 &user_base, &user_pages, &user_stack_base) != 0) {
        current->mm = old_mm;
        mm_destroy(new_mm);
        return -1;
    }

    task_set_comm(current, program);

    current->mm->pgd = user_dir;
    current->mm->start_code = user_base;
    current->mm->end_code = user_base + user_pages * 4096;
    current->mm->start_stack = user_stack_base;

    regs->eax = 0;
    regs->ebx = 0;
    regs->ecx = 0;
    regs->edx = 0;
    regs->esi = 0;
    regs->edi = 0;
    regs->ebp = 0;
    regs->ds = 0x23;
    regs->cs = 0x1B;
    regs->ss = 0x23;
    regs->eflags = 0x202;
    regs->eip = entry;
    regs->useresp = user_stack;

    vmm_switch_directory(user_dir);
    mm_destroy(old_mm);
    return 0;
}
