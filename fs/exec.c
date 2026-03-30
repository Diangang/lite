#include "linux/sched.h"
#include "linux/mm.h"
#include "linux/fdtable.h"
#include "linux/binfmts.h"
#include "linux/exit.h"
#include "linux/file.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/timer.h"
#include "asm/pgtable.h"
#include "linux/page_alloc.h"
#include "asm/page.h"
#include "linux/memlayout.h"
#include "asm/gdt.h"
#include "linux/devtmpfs.h"
#include "linux/fs.h"
#include "linux/console.h"
#include "linux/rmap.h"

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

static void ensure_private_table(pgd_t* dir, uint32_t pde_idx)
{
    if (dir[pde_idx] & PTE_PRESENT) {
        pte_t* old_table = (pte_t*)memlayout_directmap_phys_to_virt(dir[pde_idx] & ~0xFFF);
        uint32_t new_table_phys = (uint32_t)alloc_page(GFP_KERNEL);
        if (!new_table_phys)
            return;
        pte_t* new_table = (pte_t*)memlayout_directmap_phys_to_virt(new_table_phys);
        memcpy(new_table, old_table, 4096);
        dir[pde_idx] = new_table_phys | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    } else {
        uint32_t new_table_phys = (uint32_t)alloc_page(GFP_KERNEL);
        if (!new_table_phys)
            return;
        pte_t* new_table = (pte_t*)memlayout_directmap_phys_to_virt(new_table_phys);
        memset(new_table, 0, 4096);
        dir[pde_idx] = new_table_phys | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    }
}

static void ensure_private_table_range(pgd_t* dir, uint32_t start, uint32_t end)
{
    uint32_t start_idx = pgd_index(start);
    uint32_t end_idx = pgd_index(end - 1);
    for (uint32_t i = start_idx; i <= end_idx; i++)
        ensure_private_table(dir, i);
}

int kernel_load_user_program(const char* name, uint32_t* entry, uint32_t* user_stack, pgd_t** out_dir,
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
            if (!node)
                return printf("User program not found: %s\n", name), -1;
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
        if (phdr->p_vaddr >= TASK_SIZE || (phdr->p_vaddr + phdr->p_memsz) >= TASK_SIZE)
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

    pgd_t* user_dir = pgd_clone_kernel();
    if (!user_dir)
        return printf("User page directory alloc failed.\n"), kfree(buf), -1;

    uint32_t user_base = align_down(min_vaddr);
    uint32_t user_end = align_up(max_vaddr);
    uint32_t user_stack_base = USER_STACK_BASE;
    uint32_t pages = (user_end - user_base) / 4096;
    uint32_t heap_base = user_end;

    ensure_private_table_range(user_dir, user_stack_base, USER_STACK_TOP);

    if (current && !current->mm)
        current->mm = mm_create();
    if (!current || !current->mm)
        return printf("User mm alloc failed.\n"), kfree(buf), -1;
    mm_reset_mmap(current->mm);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Phdr *phdr = (Elf32_Phdr*)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != 1 || phdr->p_memsz == 0)
            continue;
        uint32_t seg_start = align_down(phdr->p_vaddr);
        uint32_t seg_end = align_up(phdr->p_vaddr + phdr->p_memsz);
        uint32_t vma_flags = VMA_READ;
        if (phdr->p_flags & PF_W) vma_flags |= VMA_WRITE;
        if (phdr->p_flags & PF_X) vma_flags |= VMA_EXEC;
        mm_add_vma(current->mm, seg_start, seg_end, vma_flags);

        ensure_private_table_range(user_dir, seg_start, seg_end);
        for (uint32_t va = seg_start; va < seg_end; va += 4096) {
            uint32_t old_phys = virt_to_phys_pgd(user_dir, (void*)va);
            if (old_phys != 0xFFFFFFFF && ((old_phys & ~0xFFF) != va))
                continue;
            void *phys = alloc_page(GFP_KERNEL);
            if (!phys)
                return printf("User program page alloc failed.\n"), kfree(buf), -1;
            map_page_ex(user_dir, phys, (void*)va, PTE_PRESENT | PTE_READ_WRITE | PTE_USER);
            rmap_add(current->mm, va, (uint32_t)phys);
        }
    }
    mm_add_vma(current->mm, user_stack_base, USER_STACK_TOP, VMA_READ | VMA_WRITE);
    mm_init_brk(current->mm, heap_base, user_stack_base);

    for (uint32_t va = user_stack_base; va < USER_STACK_TOP; va += PAGE_SIZE) {
        void *stack_phys = alloc_page(GFP_KERNEL);
        if (!stack_phys)
            return printf("User stack alloc failed.\n"), kfree(buf), -1;
        map_page_ex(user_dir, stack_phys, (void*)va,
                        PTE_PRESENT | PTE_READ_WRITE | PTE_USER);
        rmap_add(current->mm, va, (uint32_t)stack_phys);
    }

    uint32_t entry_point = ehdr->e_entry;
    pgd_t* old_dir = get_pgd_current();
    switch_pgd(user_dir);

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
                set_page_readonly_pgd(user_dir, (void*)va);
        }
    }
    switch_pgd(old_dir);
    kfree(buf);

    *entry = entry_point;
    *user_stack = USER_STACK_TOP;
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
    pgd_t* user_dir = NULL;
    uint32_t user_base = 0;
    uint32_t user_pages = 0;
    uint32_t user_stack_base = 0;
    const char *comm = task_get_current_comm();
    if (!comm) comm = "user.elf";
    if (kernel_load_user_program(comm, &entry, &stack, &user_dir,
                          &user_base, &user_pages, &user_stack_base) != 0) {
        sys_exit(1);
        return;
    }

    current->mm->pgd = user_dir;
    current->mm->start_code = user_base;
    current->mm->end_code = user_base + user_pages * 4096;
    current->mm->start_stack = user_stack_base;
    switch_pgd(user_dir);
    enter_user_mode(entry, stack);
    panic("Returned from user mode?!");
}

int task_exec_user(const char *program)
{
    if (!program)
        return -1;
    if (!current)
        return -1;

    files_init(current);
    install_stdio(devtmpfs_get_tty());

    if (!current->mm) {
        current->mm = mm_create();
        if (!current->mm)
            return -1;
    }

    set_task_comm(current, program);

    uint32_t entry = 0;
    uint32_t stack = 0;
    pgd_t* user_dir = NULL;
    uint32_t user_base = 0;
    uint32_t user_pages = 0;
    uint32_t user_stack_base = 0;
    if (kernel_load_user_program(program, &entry, &stack, &user_dir,
                          &user_base, &user_pages, &user_stack_base) != 0)
        return -1;

    current->mm->pgd = user_dir;
    current->mm->start_code = user_base;
    current->mm->end_code = user_base + user_pages * 4096;
    current->mm->start_stack = user_stack_base;
    switch_pgd(user_dir);
    enter_user_mode(entry, stack);
    panic("Returned from user mode?!");
    return -1;
}

int sys_execve(const char *program, struct pt_regs *regs)
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
    pgd_t *user_dir = NULL;
    uint32_t user_base = 0;
    uint32_t user_pages = 0;
    uint32_t user_stack_base = 0;
    if (kernel_load_user_program(program, &entry, &user_stack, &user_dir,
                                 &user_base, &user_pages, &user_stack_base) != 0) {
        current->mm = old_mm;
        mm_destroy(new_mm);
        return -1;
    }

    set_task_comm(current, program);

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

    switch_pgd(user_dir);
    mm_destroy(old_mm);
    return 0;
}
