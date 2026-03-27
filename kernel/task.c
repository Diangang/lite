#include "file.h"
#include "task.h"
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
        // Since we moved programs to /bin, we should try resolving there.
        // Also fallback to absolute path parsing directly from the VFS root.
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
            // Check absolute mount path as well
            node = vfs_resolve(basename);
            if (!node) {
                // Check original /initrd just in case
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
        if (phdr->p_type != 1) // PT_LOAD
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

struct task_struct {
    uint32_t pid;
    struct task_struct *parent;
    struct thread_struct thread;
    uint32_t wake_jiffies;
    int state;
    int time_slice;
    struct mm_struct *mm;
    int exit_code;
    int exit_state;
    uint32_t exit_info0;
    uint32_t exit_info1;
    char comm[32];
    struct fs_struct fs;
    struct files_struct files;
    uint32_t uid;
    uint32_t gid;
    uint32_t umask;
    void *waitq;
    struct task_struct *wait_next;
    struct task_struct *next;
};

static struct task_struct *task_head = NULL;
static struct task_struct *current = NULL;
static uint32_t next_task_id = 1;
static int demo_enabled = 0;
static wait_queue_t exit_waitq = {0};
static int need_resched = 0;
static uint32_t sched_switch_count = 0;

enum { TASK_TIMESLICE_TICKS = 3 };

enum {
    TASK_RUNNABLE = 0,
    TASK_SLEEPING = 1,
    TASK_BLOCKED = 2,
    TASK_ZOMBIE = 3
};

static int task_free_user_page_mapped(uint32_t *dir, uint32_t va_page);
static struct task_struct *task_find_by_pid(uint32_t pid);
static void vma_add(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags);
static int vma_range_free(struct mm_struct *mm, uint32_t start, uint32_t end);
static uint32_t vma_find_gap(struct mm_struct *mm, uint32_t size, uint32_t limit);

static uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
}

static void buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || cap == 0 || !s)
        return;
    while (*s && *off + 1 < cap) {
        buf[*off] = *s;
        (*off)++;
        s++;
    }
}

static void buf_append_u32(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    itoa((int)v, 10, tmp);
    buf_append(buf, off, cap, tmp);
}

static void buf_append_hex(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    itoa((int)v, 16, tmp);
    buf_append(buf, off, cap, "0x");
    buf_append(buf, off, cap, tmp);
}

static void task_idle(void)
{
    for (;;)
        __asm__ volatile ("hlt");
}

static void vma_list_free(struct mm_struct *mm)
{
    if (!mm)
        return;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        struct vm_area_struct *next = v->vm_next;
        kfree(v);
        v = next;
    }
    mm->mmap = NULL;
}

static void task_fdtable_init(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        task->files.fd[i].used = 0;
        task->files.fd[i].file = NULL;
    }
}

static void task_fdtable_close_all(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (task->files.fd[i].used) {
            if (task->files.fd[i].file)
                file_close(task->files.fd[i].file);
            task->files.fd[i].used = 0;
            task->files.fd[i].file = NULL;
        }
    }
}

static void task_fdtable_clone(struct task_struct *dst, struct task_struct *src)
{
    if (!dst || !src)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (src->files.fd[i].used && src->files.fd[i].file) {
            dst->files.fd[i].used = 1;
            dst->files.fd[i].file = file_dup(src->files.fd[i].file);
        }
    }
}

static struct mm_struct *mm_create(void)
{
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm)
        return NULL;
    memset(mm, 0, sizeof(*mm));
    mm->pgd = vmm_get_current_directory();
    return mm;
}

static void task_set_comm(struct task_struct *task, const char *program);

static struct vm_area_struct *vma_clone_list(struct vm_area_struct *src)
{
    struct vm_area_struct *head = NULL;
    struct vm_area_struct **tail = &head;
    while (src) {
        struct vm_area_struct *v = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
        if (!v) {
            struct vm_area_struct *n = head;
            while (n) {
                struct vm_area_struct *next = n->vm_next;
                kfree(n);
                n = next;
            }
            return NULL;
        }
        v->vm_start = src->vm_start;
        v->vm_end = src->vm_end;
        v->vm_flags = src->vm_flags;
        v->vm_next = NULL;
        *tail = v;
        tail = &v->vm_next;
        src = src->vm_next;
    }
    return head;
}

static struct mm_struct *mm_clone_cow(struct mm_struct *src)
{
    if (!src)
        return NULL;
    uint32_t *new_dir = vmm_clone_kernel_directory();
    if (!new_dir)
        return NULL;
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm) {
        pmm_free_page(new_dir);
        return NULL;
    }
    memset(mm, 0, sizeof(*mm));
    mm->pgd = new_dir;
    mm->start_code = src->start_code;
    mm->end_code = src->end_code;
    mm->start_stack = src->start_stack;
    mm->start_brk = src->start_brk;
    mm->brk = src->brk;
    mm->mmap = vma_clone_list(src->mmap);
    if (src->mmap && !mm->mmap) {
        pmm_free_page(new_dir);
        kfree(mm);
        return NULL;
    }
    struct vm_area_struct *v = src->mmap;
    while (v) {
        uint32_t start = v->vm_start & ~0xFFF;
        uint32_t end = (v->vm_end + 0xFFF) & ~0xFFF;
        for (uint32_t va = start; va < end; va += 4096) {
            uint32_t pte = vmm_get_pte_flags_ex(src->pgd, (void*)va);
            if (!(pte & PTE_PRESENT))
                continue;
            if (!(pte & PTE_USER))
                continue;
            uint32_t phys = pte & ~0xFFF;
            uint32_t flags = pte & 0xFFF;
            int was_write = (flags & PTE_READ_WRITE) != 0;
            if (was_write) {
                flags &= ~PTE_READ_WRITE;
                flags |= PTE_COW;
                vmm_update_page_flags_ex(src->pgd, (void*)va, flags);
            }
            vmm_map_page_ex(new_dir, (void*)phys, (void*)va, flags);
            pmm_ref_page((void*)phys);
        }
        v = v->vm_next;
    }
    return mm;
}

static struct pt_regs *copy_thread(uint32_t *stack, void (*entry)(void), struct pt_regs *parent_regs)
{
    if (!stack)
        return NULL;
    uint32_t stack_top = (uint32_t)stack + THREAD_SIZE;
    struct pt_regs *child_regs = (struct pt_regs*)(stack_top - sizeof(struct pt_regs));
    if (parent_regs) {
        memcpy(child_regs, parent_regs, sizeof(*parent_regs));
        child_regs->esp = stack_top;
        child_regs->ebp = 0;
        child_regs->eax = 0;
        return child_regs;
    }
    memset(child_regs, 0, sizeof(*child_regs));
    child_regs->ds = 0x10;
    child_regs->esp = stack_top;
    child_regs->ebp = 0;
    child_regs->int_no = 0;
    child_regs->err_code = 0;
    child_regs->eip = (uint32_t)entry;
    child_regs->cs = 0x08;
    child_regs->eflags = 0x202;
    child_regs->useresp = stack_top;
    child_regs->ss = 0x10;
    return child_regs;
}

static void mm_destroy(struct mm_struct *mm)
{
    if (!mm)
        return;
    if (!mm->pgd || mm->pgd == vmm_get_kernel_directory()) {
        vma_list_free(mm);
        kfree(mm);
        return;
    }

    struct vm_area_struct *v = mm->mmap;
    while (v) {
        uint32_t start = v->vm_start & ~0xFFF;
        uint32_t end = (v->vm_end + 0xFFF) & ~0xFFF;
        if (end > start) {
            for (uint32_t va = start; va < end; va += 4096)
                task_free_user_page_mapped(mm->pgd, va);
        }
        v = v->vm_next;
    }

    uint32_t *kernel_dir = vmm_get_kernel_directory();
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t pde = mm->pgd[i];
        if (!(pde & PTE_PRESENT))
            continue;

        uint32_t pde_phys = pde & ~0xFFF;
        uint32_t kernel_pde_phys = 0;
        if (kernel_dir && (kernel_dir[i] & PTE_PRESENT))
            kernel_pde_phys = kernel_dir[i] & ~0xFFF;
        if (kernel_pde_phys && kernel_pde_phys == pde_phys)
            continue;
        pmm_free_page((void*)pde_phys);
    }

    pmm_free_page(mm->pgd);
    vma_list_free(mm);
    kfree(mm);
}

// We need a helper to format dentry to path. For now, a very naive implementation for shell PWD
const char *task_get_cwd(void)
{
    static char buf[256];
    if (!current || !current->fs.pwd)
        return "/";
    struct dentry *d = current->fs.pwd;
    if (!d->parent)
        return "/"; // root

    // Reverse path build
    char tmp[256];
    int pos = 255;
    tmp[pos] = 0;

    while (d && d->parent) {
        int n = strlen(d->name);
        if (pos - n - 1 < 0)
            break; // Path too long
        pos -= n;
        memcpy(tmp + pos, d->name, n);
        pos -= 1;
        tmp[pos] = '/';
        d = d->parent;
    }

    if (pos == 255)
        strcpy(buf, "/"); else {
        // Fix double slashes if any mount root accidentally had "/" as name
        int final_pos = pos;
        if (tmp[final_pos] == '/' && tmp[final_pos + 1] == '/') final_pos++;
        strcpy(buf, tmp + final_pos);
    }
    return buf;
}

struct dentry *task_get_cwd_dentry(void)
{
    if (!current)
        return NULL;
    return current->fs.pwd;
}

struct dentry *task_get_root_dentry(void)
{
    if (!current)
        return NULL;
    return current->fs.root;
}

int task_set_cwd_dentry(struct dentry *d)
{
    if (!current || !d)
        return -1;
    if (current->fs.pwd)
        vfs_dentry_put(current->fs.pwd);
    d->refcount++;
    current->fs.pwd = d;
    return 0;
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

uint32_t task_mmap(uint32_t addr, uint32_t length, uint32_t prot)
{
    if (!current || !current->mm)
        return 0;
    if (length == 0)
        return 0;
    uint32_t len = align_up(length);
    if (len == 0)
        return 0;
    uint32_t limit = current->mm->start_stack ? current->mm->start_stack : 0xC0000000;
    if (limit <= 0x1000)
        return 0;

    if (addr != 0) {
        if (addr < 0x1000)
            return 0;
        if (addr & 0xFFF)
            return 0;
        if (addr + len > limit)
            return 0;
        if (!vma_range_free(current->mm, addr, addr + len))
            return 0;
    } else {
        addr = vma_find_gap(current->mm, len, limit);
        if (addr == 0)
            return 0;
    }

    uint32_t flags = 0;
    if (prot & VMA_READ) flags |= VMA_READ;
    if (prot & VMA_WRITE) flags |= VMA_WRITE;
    if (prot & VMA_EXEC) flags |= VMA_EXEC;
    vma_add(current->mm, addr, addr + len, flags);
    return addr;
}

int task_munmap(uint32_t addr, uint32_t length)
{
    if (!current || !current->mm)
        return -1;
    if (length == 0)
        return -1;
    if (addr < 0x1000)
        return -1;
    if (addr & 0xFFF)
        return -1;
    uint32_t len = align_up(length);
    if (len == 0)
        return -1;
    uint32_t end = addr + len;
    if (end <= addr)
        return -1;

    uint32_t flags = irq_save();
    struct vm_area_struct *v = current->mm->mmap;
    struct vm_area_struct *prev = NULL;
    while (v) {
        if (end <= v->vm_start || addr >= v->vm_end) {
            prev = v;
            v = v->vm_next;
            continue;
        }
        if (addr <= v->vm_start && end >= v->vm_end) {
            struct vm_area_struct *next = v->vm_next;
            if (prev) prev->vm_next = next;
            else current->mm->mmap = next;
            kfree(v);
            v = next;
            continue;
        }
        if (addr <= v->vm_start && end < v->vm_end) {
            v->vm_start = end;
            prev = v;
            v = v->vm_next;
            continue;
        }
        if (addr > v->vm_start && end >= v->vm_end) {
            v->vm_end = addr;
            prev = v;
            v = v->vm_next;
            continue;
        }
        if (addr > v->vm_start && end < v->vm_end) {
            struct vm_area_struct *right = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
            if (right) {
                right->vm_start = end;
                right->vm_end = v->vm_end;
                right->vm_flags = v->vm_flags;
                right->vm_next = v->vm_next;
                v->vm_end = addr;
                v->vm_next = right;
            } else {
                v->vm_end = addr;
            }
            prev = v;
            v = v->vm_next;
            continue;
        }
    }
    irq_restore(flags);

    uint32_t page_start = addr & ~0xFFF;
    uint32_t page_end = (end + 0xFFF) & ~0xFFF;
    for (uint32_t va = page_start; va < page_end; va += 4096)
        task_free_user_page_mapped(current->mm->pgd, va);
    return 0;
}

int task_fork(struct pt_regs *regs)
{
    if (!current || !current->mm || !regs)
        return -1;
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);
    if (!task || !stack) {
        if (task) kfree(task);
        if (stack) kfree(stack);
        return -1;
    }

    struct mm_struct *child_mm = mm_clone_cow(current->mm);
    if (!child_mm) {
        kfree(stack);
        kfree(task);
        return -1;
    }

    task->pid = next_task_id++;
    task->parent = current;
    task->thread.regs = copy_thread(stack, NULL, regs);
    if (!task->thread.regs) {
        mm_destroy(child_mm);
        kfree(stack);
        kfree(task);
        return -1;
    }
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    task->wake_jiffies = 0;
    task->state = TASK_RUNNABLE;
    task->time_slice = TASK_TIMESLICE_TICKS;
    task->mm = child_mm;
    task->exit_code = 0;
    task->exit_state = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task_set_comm(task, current->comm);
    task->fs.pwd = current->fs.pwd;
    if (task->fs.pwd) task->fs.pwd->refcount++;
    task->fs.root = current->fs.root;
    if (task->fs.root) task->fs.root->refcount++;

    if (current) {
        task->uid = current->uid;
        task->gid = current->gid;
        task->umask = current->umask;
    } else {
        task->uid = 0;
        task->gid = 0;
        task->umask = 022;
    }
    task_fdtable_init(task);
    task_fdtable_clone(task, current);
    task->uid = current->uid;
    task->gid = current->gid;
    task->umask = current->umask;
    task->waitq = NULL;
    task->wait_next = NULL;

    uint32_t flags = irq_save();
    task->next = task_head->next;
    task_head->next = task;
    irq_restore(flags);

    return (int)task->pid;
}

static int task_free_user_page_mapped(uint32_t *dir, uint32_t va_page)
{
    if (!dir)
        return 0;
    uint32_t pde_idx = va_page / (1024 * 4096);
    uint32_t pte_idx = (va_page % (1024 * 4096)) / 4096;
    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT))
        return 0;
    uint32_t *table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT))
        return 0;
    if (!(pte & PTE_USER))
        return 0;

    uint32_t phys = pte & ~0xFFF;
    table[pte_idx] = 0;
    pmm_free_page((void*)phys);
    return 1;
}

static void vma_add(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags)
{
    if (!mm)
        return;
    if (start >= end)
        return;
    struct vm_area_struct *v = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
    if (!v)
        return;
    v->vm_start = start;
    v->vm_end = end;
    v->vm_flags = flags;
    v->vm_next = mm->mmap;
    mm->mmap = v;
}

static int vma_range_free(struct mm_struct *mm, uint32_t start, uint32_t end)
{
    if (!mm)
        return 0;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        if (end <= v->vm_start || start >= v->vm_end) {
            v = v->vm_next;
            continue;
        }
        return 0;
    }
    return 1;
}

static uint32_t vma_find_gap(struct mm_struct *mm, uint32_t size, uint32_t limit)
{
    if (!mm)
        return 0;
    if (size == 0)
        return 0;
    uint32_t start = mm->brk ? align_up(mm->brk) : 0x400000;
    if (mm->start_code && mm->end_code) {
        uint32_t min = mm->end_code;
        if (start < min) start = align_up(min);
    }
    if (start < 0x1000) start = 0x1000;
    if (limit <= start)
        return 0;
    while (start + size <= limit) {
        if (vma_range_free(mm, start, start + size))
            return start;
        start += 0x1000;
    }
    return 0;
}

static struct vm_area_struct *vma_find_heap(struct mm_struct *mm)
{
    if (!mm)
        return NULL;
    if (!mm->start_brk)
        return NULL;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        if (v->vm_start == mm->start_brk && (v->vm_flags & (VMA_READ | VMA_WRITE)) == (VMA_READ | VMA_WRITE))
            return v;
        v = v->vm_next;
    }
    return NULL;
}

void task_user_vmas_reset(void)
{
    if (!current)
        return;
    if (!current->mm)
        return;
    vma_list_free(current->mm);
}

void task_user_vma_add(uint32_t start, uint32_t end, uint32_t flags)
{
    if (!current)
        return;
    if (!current->mm)
        return;
    vma_add(current->mm, start, end, flags);
}

static void task_demo_a(void)
{
    for (;;) {
        if (task_get_demo_enabled())
            printf("A");
        task_sleep(5);
        task_yield();
    }
}

static void task_demo_b(void)
{
    for (;;) {
        if (task_get_demo_enabled())
            printf("B");
        task_sleep(5);
        task_yield();
    }
}

void init_task(void)
{
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);

    if (!task || !stack)
        panic("TASK: Failed to initialize tasking.");

    task->pid = 0;
    task->parent = NULL;
    task->thread.regs = copy_thread(stack, task_idle, NULL);
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    task->wake_jiffies = 0;
    task->state = TASK_RUNNABLE;
    task->time_slice = TASK_TIMESLICE_TICKS;
    task->mm = NULL;
    task->exit_code = 0;
    task->exit_state = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task->comm[0] = 0;
    task->fs.pwd = vfs_root_dentry;
    if (task->fs.pwd)
        task->fs.pwd->refcount++;
    task->fs.root = vfs_root_dentry;
    if (task->fs.root)
        task->fs.root->refcount++;
    task->uid = 0;
    task->gid = 0;
    task->umask = 022;
    task_fdtable_init(task);
    task->waitq = NULL;
    task->wait_next = NULL;
    task->next = task;

    task_head = task;
    current = task;
    wait_queue_init(&exit_waitq);
    tss_set_kernel_stack((uint32_t)current->thread.sp0);

    task_create(task_demo_a);
    task_create(task_demo_b);
}

void sched_init(void)
{
    init_task();
}

void fork_init(void)
{
    if (!task_head || !current)
        panic("fork_init before sched_init.");
}

static void task_set_comm(struct task_struct *task, const char *program)
{
    if (!task)
        return;
    task->comm[0] = 0;
    if (!program)
        return;
    uint32_t i = 0;
    while (program[i] && i + 1 < sizeof(task->comm)) {
        task->comm[i] = program[i];
        i++;
    }
    task->comm[i] = 0;
}

static int task_create_internal(void (*entry)(void), const char *program)
{
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);

    if (!task || !stack)
        return printf("TASK: Failed to create task.\n"), -1;

    task->pid = next_task_id++;
    task->parent = current;
    task->thread.regs = copy_thread(stack, entry, NULL);
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    task->wake_jiffies = 0;
    task->state = TASK_RUNNABLE;
    task->time_slice = TASK_TIMESLICE_TICKS;
    task->mm = NULL;
    if (program) {
        task->mm = mm_create();
        if (!task->mm) {
            kfree(stack);
            kfree(task);
            return -1;
        }
    }
    task->exit_code = 0;
    task->exit_state = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task->uid = current ? current->uid : 0;
    task->gid = current ? current->gid : 0;
    task->umask = current ? current->umask : 022;
    task_set_comm(task, program);
    if (current) {
        task->fs.pwd = current->fs.pwd;
        if (task->fs.pwd) task->fs.pwd->refcount++;
        task->fs.root = current->fs.root;
        if (task->fs.root) task->fs.root->refcount++;
    } else {
        task->fs.pwd = vfs_root_dentry;
        if (task->fs.pwd) task->fs.pwd->refcount++;
        task->fs.root = vfs_root_dentry;
        if (task->fs.root) task->fs.root->refcount++;
    }
    task_fdtable_init(task);
    if (program) {
        struct task_struct *prev = current;
        current = task;
        task_install_stdio(devfs_get_console());
        current = prev;
    }
    task->waitq = NULL;
    task->wait_next = NULL;

    uint32_t flags = irq_save();
    task->next = task_head->next;
    task_head->next = task;
    irq_restore(flags);

    return (int)task->pid;
}

int task_create(void (*entry)(void))
{
    return task_create_internal(entry, NULL);
}

int task_create_user(const char *program)
{
    extern void user_task(void);
    return task_create_internal(user_task, program);
}

static void task_free_user_memory(struct task_struct *task)
{
    if (!task)
        return;
    if (!task->mm)
        return;
    mm_destroy(task->mm);
    task->mm = NULL;
}

static void task_destroy(struct task_struct *prev, struct task_struct *task)
{
    if (!prev || !task)
        return;
    if (!task_head || !current)
        return;
    if (task == current)
        return;

    uint32_t flags = irq_save();
    struct task_struct *next = task->next;
    if (task == task_head)
        task_head = next;
    prev->next = next;
    irq_restore(flags);

    task_fdtable_close_all(task);
    task_free_user_memory(task);
    if (task->thread.sp0)
        kfree((void*)((uint32_t)task->thread.sp0 - THREAD_SIZE));
    kfree(task);
}

void wait_queue_init(wait_queue_t *q)
{
    if (!q)
        return;
    q->head = NULL;
}

void wait_queue_block(wait_queue_t *q)
{
    if (!q)
        return;
    if (!current)
        return;
    uint32_t flags = irq_save();
    wait_queue_block_locked(q);
    irq_restore(flags);
}

void wait_queue_block_locked(wait_queue_t *q)
{
    if (!q)
        return;
    if (!current)
        return;
    if (current->waitq == q)
        return;
    if (current->state == TASK_ZOMBIE)
        return;

    current->waitq = q;
    current->wait_next = (struct task_struct*)q->head;
    q->head = current;
    current->state = TASK_BLOCKED;
}

void wait_queue_wake_all(wait_queue_t *q)
{
    if (!q)
        return;

    uint32_t flags = irq_save();
    struct task_struct *t = (struct task_struct*)q->head;
    q->head = NULL;
    while (t) {
        struct task_struct *next = t->wait_next;
        t->wait_next = NULL;
        t->waitq = NULL;
        if (t->state == TASK_BLOCKED)
            t->state = TASK_RUNNABLE;
        t = next;
    }
    irq_restore(flags);
}

void task_tick(void)
{
    uint32_t now = timer_get_ticks();
    struct task_struct *t = task_head;
    if (!t)
        return;

    do {
        if (t->state == TASK_SLEEPING) {
            if ((int32_t)(now - t->wake_jiffies) >= 0)
                t->state = TASK_RUNNABLE;
        }
        t = t->next;
    } while (t && t != task_head);

    if (!current)
        return;
    if (current->state != TASK_RUNNABLE) {
        need_resched = 1;
        return;
    }

    current->time_slice--;
    if (current->time_slice <= 0)
        need_resched = 1;
}

struct pt_regs *task_schedule(struct pt_regs *regs)
{
    if (!current)
        return regs;

    current->thread.regs = regs;
    if (current->state == TASK_RUNNABLE && !need_resched)
        return current->thread.regs;

    need_resched = 0;
    current->time_slice = TASK_TIMESLICE_TICKS;

    struct task_struct *candidate = current->next;
    while (candidate) {
        if (candidate->state == TASK_RUNNABLE) {
            if (candidate != current)
                sched_switch_count++;
            current = candidate;
            if (current->mm && current->mm->pgd) {
                if (current->mm->pgd != vmm_get_current_directory())
                    vmm_switch_directory(current->mm->pgd);
            } else {
                uint32_t *kdir = vmm_get_kernel_directory();
                if (kdir && kdir != vmm_get_current_directory())
                    vmm_switch_directory(kdir);
            }
            tss_set_kernel_stack((uint32_t)current->thread.sp0);
            current->time_slice = TASK_TIMESLICE_TICKS;
            return current->thread.regs;
        }

        candidate = candidate->next;
        if (candidate == current)
            break;
    }

    return current->thread.regs;
}

void task_sleep(uint32_t ticks)
{
    if (!current)
        return;
    if (ticks == 0)
        return;
    current->wake_jiffies = timer_get_ticks() + ticks;
    current->state = TASK_SLEEPING;
    need_resched = 1;
}

void task_yield(void)
{
    need_resched = 1;
    if (current && current->state != TASK_RUNNABLE) {
        while (current->state != TASK_RUNNABLE) {
            __asm__ volatile("sti; hlt");
        }
    } else {
        // We MUST force a schedule! If we don't, we just return to caller.
        // If caller loops (like waitpid might), it just spins!
        // waitpid does a for(;;) loop and calls wait_queue_block_locked -> task_yield.
        // Wait, waitpid changes state to BLOCKED, so it enters the first branch.
        // But what if it's already RUNNABLE for some reason?
        // Let's just use hlt to wait for next tick.
        __asm__ volatile("sti; hlt");
    }
}

void task_set_demo_enabled(int enabled)
{
    demo_enabled = enabled ? 1 : 0;
}

int task_get_demo_enabled(void)
{
    return demo_enabled;
}

void task_set_current_page_directory(uint32_t* dir)
{
    if (!current || !dir)
        return;
    if (!current->mm) {
        current->mm = mm_create();
        if (!current->mm)
            return;
    }
    current->mm->pgd = dir;
}

void task_set_user_info(uint32_t base, uint32_t pages, uint32_t stack_base)
{
    if (!current)
        return;
    if (!current->mm) {
        current->mm = mm_create();
        if (!current->mm)
            return;
    }
    current->mm->start_code = base;
    current->mm->end_code = base + pages * 4096;
    current->mm->start_stack = stack_base;
    if (!current->mm->mmap) {
        if (base && pages)
            vma_add(current->mm, base, base + pages * 4096, VMA_READ | VMA_WRITE | VMA_EXEC);
        if (stack_base)
            vma_add(current->mm, stack_base, stack_base + 4096, VMA_READ | VMA_WRITE);
    }
}

void task_get_user_info(uint32_t *base, uint32_t *pages, uint32_t *stack_base)
{
    if (!current || !current->mm) {
        if (base) *base = 0;
        if (pages) *pages = 0;
        if (stack_base) *stack_base = 0;
        return;
    }
    if (base) *base = current->mm->start_code;
    if (pages) *pages = current->mm->end_code > current->mm->start_code ? (current->mm->end_code - current->mm->start_code) / 4096 : 0;
    if (stack_base) *stack_base = current->mm->start_stack;
}

int task_user_vma_allows(uint32_t addr, int is_write, int is_exec)
{
    if (!current || !current->mm)
        return 0;
    struct vm_area_struct *v = current->mm->mmap;
    while (v) {
        if (addr >= v->vm_start && addr < v->vm_end) {
            if (is_exec && !(v->vm_flags & VMA_EXEC))
                return 0;
            if (is_write && !(v->vm_flags & VMA_WRITE))
                return 0;
            if (!is_write && !(v->vm_flags & VMA_READ))
                return 0;
            return 1;
        }
        v = v->vm_next;
    }
    return 0;
}

void task_exit(void)
{
    if (!current)
        return;
    if (current->pid == 0)
        return;
    task_exit_with_status(0);
}

void task_exit_with_status(int code)
{
    task_exit_with_reason(code, TASK_EXIT_NORMAL, 0, 0);
}

void task_exit_with_reason(int code, int reason, uint32_t info0, uint32_t info1)
{
    if (!current)
        return;
    if (current->pid == 0)
        return;
    if (current->state == TASK_ZOMBIE)
        return;
    current->exit_code = code;
    current->exit_state = reason;
    current->exit_info0 = info0;
    current->exit_info1 = info1;
    current->state = TASK_ZOMBIE;
    wait_queue_wake_all(&exit_waitq);
    need_resched = 1;

    // We are exiting, we MUST never return to user space!
    // Just loop here waiting for the timer tick to kill us.
    // If we return to an ISR, the ISR will handle scheduling.
    // If we return to syscall, the syscall handler will handle scheduling.
    // Wait, if we return from page_fault_handler, it calls task_schedule() but doesn't loop!
    // Actually page_fault_handler DOES return to assembly, which pops registers.
    // If we don't block here, we return to the dead task!
    // But page_fault_handler calls `return task_schedule(regs);`
    // So it WILL schedule a new task.
    // What if we are called from sys_exit?
    // sys_exit is just a normal syscall. If we return, syscall handler calls task_schedule(regs) and returns to the NEW task!
    // So returning is perfectly safe and correct!
    return;
}

int task_wait(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1)
{
    if (id == 0)
        return -1;
    if (!task_head || !current)
        return -1;
    if (current->pid == id)
        return -1;

    for (;;) {
        uint32_t flags = irq_save();
        struct task_struct *prev = task_head;
        struct task_struct *t = task_head->next;
        while (t && t != task_head) {
            if (t->pid == id) {
                if (t->state == TASK_ZOMBIE) {
                    irq_restore(flags);
                    if (out_code) *out_code = t->exit_code;
                    if (out_reason) *out_reason = t->exit_state;
                    if (out_info0) *out_info0 = t->exit_info0;
                    if (out_info1) *out_info1 = t->exit_info1;
                    task_destroy(prev, t);
                    return 0;
                }
                break;
            }
            prev = t;
            t = t->next;
        }
        if (!t || t == task_head) {
            irq_restore(flags);
            return -1;
        }
        wait_queue_block_locked(&exit_waitq);
        irq_restore(flags);

        // Use soft interrupt to yield immediately! Wait, we just removed int 0x80 from task_yield.
        // If we call task_yield, it will loop in sti; hlt until RUNNABLE.
        // This is perfectly correct!
        void task_yield(void);
        task_yield();
    }
}

int task_kill(uint32_t id, int sig)
{
    if (id == 0)
        return -1;
    if (!task_head)
        return -1;
    uint32_t flags = irq_save();
    struct task_struct *t = task_find_by_pid(id);
    if (!t || t->pid == 0) {
        irq_restore(flags);
        return -1;
    }
    if (t->state == TASK_ZOMBIE) {
        irq_restore(flags);
        return 0;
    }
    if (!t->mm) {
        irq_restore(flags);
        return -1;
    }
    if (t == current) {
        irq_restore(flags);
        task_exit_with_reason(128 + sig, TASK_EXIT_SIGNAL, (uint32_t)sig, 0);
        return 0;
    }
    t->exit_code = 128 + sig;
    t->exit_state = TASK_EXIT_SIGNAL;
    t->exit_info0 = (uint32_t)sig;
    t->exit_info1 = 0;
    t->state = TASK_ZOMBIE;
    wait_queue_wake_all(&exit_waitq);
    irq_restore(flags);
    return 0;
}

const char *task_get_current_comm(void)
{
    if (!current)
        return NULL;
    if (!current->comm[0])
        return NULL;
    return current->comm;
}

void task_user_heap_init(uint32_t heap_base, uint32_t stack_base)
{
    if (!current)
        return;
    if (!current->mm)
        return;
    if (heap_base < 0x1000)
        return;
    if (heap_base >= 0xC0000000)
        return;
    if (stack_base && heap_base + 0x1000 >= stack_base)
        return;
    current->mm->start_brk = heap_base;
    current->mm->brk = heap_base;

    struct vm_area_struct *heap = vma_find_heap(current->mm);
    if (!heap)
        vma_add(current->mm, heap_base, heap_base, VMA_READ | VMA_WRITE);
}

uint32_t task_brk(uint32_t new_end)
{
    if (!current)
        return 0;
    if (!current->mm)
        return 0;
    if (current->mm->start_brk == 0)
        return 0;

    if (new_end == 0)
        return current->mm->brk;

    if (new_end < current->mm->start_brk)
        return current->mm->brk;
    if (new_end >= 0xC0000000)
        return current->mm->brk;
    if (current->mm->start_stack && align_up(new_end) + 0x1000 > current->mm->start_stack)
        return current->mm->brk;

    current->mm->brk = new_end;

    struct vm_area_struct *heap = vma_find_heap(current->mm);
    if (heap) {
        uint32_t end = align_up(new_end);
        if (end < heap->vm_start) end = heap->vm_start;
        heap->vm_end = end;
    }

    return current->mm->brk;
}

uint32_t task_get_current_id(void)
{
    if (!current)
        return 0;
    return current->pid;
}

int task_current_is_user(void)
{
    if (!current)
        return 0;
    return current->mm != NULL;
}

int task_should_resched(void)
{
    if (!current)
        return 0;
    if (current->state != TASK_RUNNABLE)
        return 1;
    return need_resched != 0;
}

uint32_t task_get_uid(void)
{
    if (!current)
        return 0;
    return current->uid;
}

uint32_t task_get_gid(void)
{
    if (!current)
        return 0;
    return current->gid;
}

uint32_t task_get_umask(void)
{
    if (!current)
        return 022;
    return current->umask;
}

uint32_t task_set_umask(uint32_t mask)
{
    if (!current)
        return 022;
    uint32_t old = current->umask;
    current->umask = mask & 0777;
    return old;
}

uint32_t task_get_switch_count(void)
{
    return sched_switch_count;
}

uint32_t task_dump_tasks(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head) {
        const char *s = "No tasks.\n";
        uint32_t n = (uint32_t)strlen(s);
        if (n > len) n = len;
        memcpy(buf, s, n);
        return n;
    }

    uint32_t off = 0;
    buf_append(buf, &off, len, "PID   STATE     WAKE    CURRENT  NAME\n");
    struct task_struct *task = task_head;
    do {
        const char *state = "RUNNABLE";
        if (task->state == TASK_SLEEPING) state = "SLEEPING";
        else if (task->state == TASK_BLOCKED) state = "BLOCKED";
        else if (task->state == TASK_ZOMBIE) state = "ZOMBIE";

        const char *name = task->comm[0] ? task->comm : "-";
        buf_append_u32(buf, &off, len, task->pid);
        buf_append(buf, &off, len, "    ");
        buf_append(buf, &off, len, state);
        buf_append(buf, &off, len, "  ");
        buf_append_u32(buf, &off, len, task->wake_jiffies);
        buf_append(buf, &off, len, "    ");
        buf_append(buf, &off, len, task == current ? "yes" : "no");
        buf_append(buf, &off, len, "     ");
        buf_append(buf, &off, len, name);
        buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        task = task->next;
    } while (task && task != task_head);
    if (off < len) buf[off] = 0;
    return off;
}

uint32_t task_dump_maps(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!current || !current->mm)
        return 0;

    uint32_t off = 0;
    struct vm_area_struct *v = current->mm->mmap;
    while (v) {
        buf_append_hex(buf, &off, len, v->vm_start);
        buf_append(buf, &off, len, "-");
        buf_append_hex(buf, &off, len, v->vm_end);
        buf_append(buf, &off, len, " ");
        buf_append(buf, &off, len, (v->vm_flags & VMA_READ) ? "r" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_WRITE) ? "w" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_EXEC) ? "x" : "-");
        buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        v = v->vm_next;
    }
    if (off < len) buf[off] = 0;
    return off;
}

uint32_t task_dump_maps_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;

    uint32_t flags = irq_save();
    struct task_struct *t = task_head;
    int found = 0;
    do {
        if (t->pid == pid) {
            found = 1;
            break;
        }
        t = t->next;
    } while (t && t != task_head);
    if (!found) {
        irq_restore(flags);
        return 0;
    }
    if (!t->mm) {
        irq_restore(flags);
        return 0;
    }

    uint32_t off = 0;
    struct vm_area_struct *v = t->mm->mmap;
    while (v) {
        buf_append_hex(buf, &off, len, v->vm_start);
        buf_append(buf, &off, len, "-");
        buf_append_hex(buf, &off, len, v->vm_end);
        buf_append(buf, &off, len, " ");
        buf_append(buf, &off, len, (v->vm_flags & VMA_READ) ? "r" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_WRITE) ? "w" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_EXEC) ? "x" : "-");
        buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        v = v->vm_next;
    }
    if (off < len) buf[off] = 0;
    irq_restore(flags);
    return off;
}

uint32_t task_dump_stat_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;

    uint32_t flags = irq_save();
    struct task_struct *t = task_head;
    int found = 0;
    do {
        if (t->pid == pid) {
            found = 1;
            break;
        }
        t = t->next;
    } while (t && t != task_head);
    if (!found) {
        irq_restore(flags);
        return 0;
    }

    const char *name = t->comm[0] ? t->comm : "-";
    char state = 'R';
    if (t->state == TASK_SLEEPING) state = 'S';
    else if (t->state == TASK_BLOCKED) state = 'D';
    else if (t->state == TASK_ZOMBIE) state = 'Z';

    uint32_t off = 0;
    buf_append_u32(buf, &off, len, t->pid);
    buf_append(buf, &off, len, " (");
    buf_append(buf, &off, len, name);
    buf_append(buf, &off, len, ") ");
    char st[2] = { state, 0 };
    buf_append(buf, &off, len, st);
    buf_append(buf, &off, len, " ");
    buf_append_u32(buf, &off, len, t->wake_jiffies);
    buf_append(buf, &off, len, "\n");
    if (off < len) buf[off] = 0;
    irq_restore(flags);
    return off;
}

static struct task_struct *task_find_by_pid(uint32_t pid)
{
    if (!task_head)
        return NULL;
    struct task_struct *t = task_head;
    do {
        if (t->pid == pid)
            return t;
        t = t->next;
    } while (t && t != task_head);
    return NULL;
}

uint32_t task_dump_cmdline_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();

    uint32_t flags = irq_save();
    struct task_struct *t = task_find_by_pid(pid);
    if (!t) {
        irq_restore(flags);
        return 0;
    }
    uint32_t off = 0;
    const char *name = t->comm[0] ? t->comm : "-";
    buf_append(buf, &off, len, name);
    buf_append(buf, &off, len, "\n");
    if (off < len) buf[off] = 0;
    irq_restore(flags);
    return off;
}

uint32_t task_dump_status_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();

    uint32_t flags = irq_save();
    struct task_struct *t = task_find_by_pid(pid);
    if (!t) {
        irq_restore(flags);
        return 0;
    }

    const char *name = t->comm[0] ? t->comm : "-";
    const char *state = "R";
    if (t->state == TASK_SLEEPING) state = "S";
    else if (t->state == TASK_BLOCKED) state = "D";
    else if (t->state == TASK_ZOMBIE) state = "Z";

    uint32_t off = 0;
    buf_append(buf, &off, len, "Name:\t");
    buf_append(buf, &off, len, name);
    buf_append(buf, &off, len, "\nState:\t");
    buf_append(buf, &off, len, state);
    buf_append(buf, &off, len, "\nPid:\t");
    buf_append_u32(buf, &off, len, t->pid);
    buf_append(buf, &off, len, "\nType:\t");
    buf_append(buf, &off, len, t->mm ? "user" : "kthread");
    buf_append(buf, &off, len, "\nCwd:\t");
    // We cannot dump absolute path easily right now without a helper, dummy output
    buf_append(buf, &off, len, "/");
    buf_append(buf, &off, len, "\n");
    if (off < len) buf[off] = 0;
    irq_restore(flags);
    return off;
}

uint32_t task_dump_cwd_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();

    uint32_t flags = irq_save();
    struct task_struct *t = task_find_by_pid(pid);
    if (!t) {
        irq_restore(flags);
        return 0;
    }
    uint32_t off = 0;
    buf_append(buf, &off, len, "/");
    buf_append(buf, &off, len, "\n");
    if (off < len) buf[off] = 0;
    irq_restore(flags);
    return off;
}

uint32_t task_dump_fd_pid(uint32_t pid, uint32_t fd, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    if (fd >= TASK_FD_MAX)
        return 0;

    uint32_t flags = irq_save();
    struct task_struct *t = task_find_by_pid(pid);
    if (!t) {
        irq_restore(flags);
        return 0;
    }
    if (!t->files.fd[fd].used || !t->files.fd[fd].file || !t->files.fd[fd].file->dentry->inode) {
        irq_restore(flags);
        return 0;
    }

    uint32_t off = 0;
    buf_append(buf, &off, len, t->files.fd[fd].file->dentry->name);
    buf_append(buf, &off, len, "\n");
    if (off < len) buf[off] = 0;
    irq_restore(flags);
    return off;
}

int task_fd_alloc(struct file *file)
{
    if (!current || !file)
        return -1;
    for (int i = 3; i < TASK_FD_MAX; i++) {
        if (!current->files.fd[i].used) {
            current->files.fd[i].used = 1;
            current->files.fd[i].file = file;
            return i;
        }
    }
    return -1;
}

struct fd_struct *task_fd_get(int fd)
{
    if (!current)
        return NULL;
    if (fd < 0 || fd >= TASK_FD_MAX)
        return NULL;
    if (!current->files.fd[fd].used)
        return NULL;
    if (!current->files.fd[fd].file)
        return NULL;
    return &current->files.fd[fd];
}

int task_fd_close(int fd)
{
    if (fd < 3)
        return -1;
    struct fd_struct *d = task_fd_get(fd);
    if (!d)
        return -1;
    if (d->file) file_close(d->file);
    d->used = 0;
    d->file = NULL;
    return 0;
}

void task_install_stdio(struct inode *console)
{
    if (!current)
        return;
    if (!console)
        return;

    current->files.fd[0].used = 1;
    current->files.fd[0].file = file_open_node(console, 0);

    current->files.fd[1].used = 1;
    current->files.fd[1].file = file_open_node(console, 0);

    current->files.fd[2].used = 1;
    current->files.fd[2].file = file_open_node(console, 0);
}

void task_list(void)
{
    if (!task_head)
        return (void)printf("No tasks.\n");

    printf("PID   STATE     WAKE    CURRENT\n");
    struct task_struct *task = task_head;
    do {
        const char *state = "RUNNABLE";
        if (task->state == TASK_SLEEPING)
        state = "SLEEPING"; else if (task->state == TASK_BLOCKED)
        state = "BLOCKED"; else if (task->state == TASK_ZOMBIE)
        state = "ZOMBIE";
        printf("%d    %s  %d    %s\n",
               task->pid,
               state,
               task->wake_jiffies,
               task == current ? "yes" : "no");
        task = task->next;
    } while (task && task != task_head);
}
