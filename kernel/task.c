#include "file.h"
#include "task_internal.h"
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

static uint32_t align_up(uint32_t value)
{
    return (value + 0xFFF) & ~0xFFF;
}

struct task_struct *task_head = NULL;
struct task_struct *current = NULL;
uint32_t next_task_id = 1;
wait_queue_t exit_waitq = {0};
int need_resched = 0;
uint32_t sched_switch_count = 0;

static int task_free_user_page_mapped(uint32_t *dir, uint32_t va_page);
struct task_struct *task_find_by_pid(uint32_t pid);
static void vma_add(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags);
static int vma_range_free(struct mm_struct *mm, uint32_t start, uint32_t end);
static uint32_t vma_find_gap(struct mm_struct *mm, uint32_t size, uint32_t limit);

uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
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

void task_fdtable_init(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        task->files.fd[i].used = 0;
        task->files.fd[i].file = NULL;
    }
}

void task_fdtable_close_all(struct task_struct *task)
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

void task_fdtable_clone(struct task_struct *dst, struct task_struct *src)
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

struct mm_struct *mm_create(void)
{
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm)
        return NULL;
    memset(mm, 0, sizeof(*mm));
    mm->pgd = vmm_get_current_directory();
    return mm;
}

void mm_destroy(struct mm_struct *mm)
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

struct task_struct *task_find_by_pid(uint32_t pid)
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

void fork_init(void)
{
    if (!task_head || !current)
        panic("fork_init before sched_init.");
}

void sched_init(void)
{
    init_task();
}
