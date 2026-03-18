#include "task.h"
#include "kheap.h"
#include "kernel.h"
#include "libc.h"
#include "timer.h"
#include "vmm.h"
#include "pmm.h"
#include "shell.h"
#include "syscall.h"
#include "tss.h"

typedef struct vma {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
    struct vma *next;
} vma_t;

typedef struct task {
    uint32_t id;
    uint32_t parent_id;
    registers_t *regs;
    uint32_t *stack;
    uint32_t wake_tick;
    int state;
    int timeslice;
    uint32_t* page_directory;
    uint32_t user_base;
    uint32_t user_pages;
    uint32_t user_stack_base;
    uint32_t heap_base;
    uint32_t heap_brk;
    int exit_code;
    int exit_reason;
    uint32_t exit_info0;
    uint32_t exit_info1;
    char program[32];
    vma_t *vma_list;
    void *waitq;
    struct task *wait_next;
    struct task *next;
} task_t;

static task_t *task_head = NULL;
static task_t *task_current = NULL;
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
    if (!buf || !off || cap == 0 || !s) return;
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
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void vma_list_free(task_t *task)
{
    if (!task) return;
    vma_t *v = task->vma_list;
    while (v) {
        vma_t *next = v->next;
        kfree(v);
        v = next;
    }
    task->vma_list = NULL;
}

static void vma_add(task_t *task, uint32_t start, uint32_t end, uint32_t flags)
{
    if (!task) return;
    if (start >= end) return;
    vma_t *v = (vma_t*)kmalloc(sizeof(vma_t));
    if (!v) return;
    v->start = start;
    v->end = end;
    v->flags = flags;
    v->next = task->vma_list;
    task->vma_list = v;
}

static vma_t *vma_find_heap(task_t *task)
{
    if (!task) return NULL;
    if (!task->heap_base) return NULL;
    vma_t *v = task->vma_list;
    while (v) {
        if (v->start == task->heap_base && (v->flags & (VMA_READ | VMA_WRITE)) == (VMA_READ | VMA_WRITE)) {
            return v;
        }
        v = v->next;
    }
    return NULL;
}

static uint32_t align_up(uint32_t value)
{
    return (value + 0xFFF) & ~0xFFF;
}

void task_user_vmas_reset(void)
{
    if (!task_current) return;
    vma_list_free(task_current);
}

void task_user_vma_add(uint32_t start, uint32_t end, uint32_t flags)
{
    if (!task_current) return;
    vma_add(task_current, start, end, flags);
}

static registers_t *task_build_regs(uint32_t *stack, void (*entry)(void))
{
    uint32_t stack_top = (uint32_t)stack + 4096;
    registers_t *regs = (registers_t*)(stack_top - sizeof(registers_t));

    memset(regs, 0, sizeof(*regs));
    regs->ds = 0x10;
    regs->esp = stack_top;
    regs->ebp = 0;
    regs->int_no = 0;
    regs->err_code = 0;
    regs->eip = (uint32_t)entry;
    regs->cs = 0x08;
    regs->eflags = 0x202;
    regs->useresp = stack_top;
    regs->ss = 0x10;

    return regs;
}

void tasking_init(void)
{
    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    uint32_t *stack = (uint32_t*)kmalloc(4096);

    if (!task || !stack) {
        printf("TASK: Failed to initialize tasking.\n");
        for(;;);
    }

    task->id = 0;
    task->parent_id = 0;
    task->regs = task_build_regs(stack, task_idle);
    task->stack = stack;
    task->wake_tick = 0;
    task->state = TASK_RUNNABLE;
    task->timeslice = TASK_TIMESLICE_TICKS;
    task->page_directory = vmm_get_current_directory();
    task->user_base = 0;
    task->user_pages = 0;
    task->user_stack_base = 0;
    task->heap_base = 0;
    task->heap_brk = 0;
    task->exit_code = 0;
    task->exit_reason = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task->program[0] = 0;
    task->vma_list = NULL;
    task->waitq = NULL;
    task->wait_next = NULL;
    task->next = task;

    task_head = task;
    task_current = task;
    wait_queue_init(&exit_waitq);
    tss_set_kernel_stack((uint32_t)task_current->stack + 4096);
}

static void task_set_program_name(task_t *task, const char *program)
{
    if (!task) return;
    task->program[0] = 0;
    if (!program) return;
    uint32_t i = 0;
    while (program[i] && i + 1 < sizeof(task->program)) {
        task->program[i] = program[i];
        i++;
    }
    task->program[i] = 0;
}

static int task_create_internal(void (*entry)(void), const char *program)
{
    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    uint32_t *stack = (uint32_t*)kmalloc(4096);

    if (!task || !stack) {
        printf("TASK: Failed to create task.\n");
        return -1;
    }

    task->id = next_task_id++;
    task->parent_id = task_current ? task_current->id : 0;
    task->regs = task_build_regs(stack, entry);
    task->stack = stack;
    task->wake_tick = 0;
    task->state = TASK_RUNNABLE;
    task->timeslice = TASK_TIMESLICE_TICKS;
    task->page_directory = vmm_get_current_directory();
    task->user_base = 0;
    task->user_pages = 0;
    task->user_stack_base = 0;
    task->heap_base = 0;
    task->heap_brk = 0;
    task->exit_code = 0;
    task->exit_reason = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task_set_program_name(task, program);
    task->vma_list = NULL;
    task->waitq = NULL;
    task->wait_next = NULL;

    uint32_t flags = irq_save();
    task->next = task_head->next;
    task_head->next = task;
    irq_restore(flags);

    return (int)task->id;
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

static void task_free_user_memory(task_t *task)
{
    if (!task) return;
    if (!task->page_directory || task->page_directory == vmm_get_kernel_directory()) return;

    if (task->user_pages && task->user_base) {
        for (uint32_t i = 0; i < task->user_pages; i++) {
            uint32_t va = task->user_base + i * 4096;
            uint32_t phys = vmm_virt_to_phys_ex(task->page_directory, (void*)va);
            if (phys != 0xFFFFFFFF) {
                pmm_free_page((void*)(phys & ~0xFFF));
            }
        }
    }
    if (task->user_stack_base) {
        uint32_t phys = vmm_virt_to_phys_ex(task->page_directory, (void*)task->user_stack_base);
        if (phys != 0xFFFFFFFF) {
            pmm_free_page((void*)(phys & ~0xFFF));
        }
    }

    if (task->user_pages && task->user_base) {
        uint32_t start = task->user_base;
        uint32_t end = task->user_base + task->user_pages * 4096;
        uint32_t start_idx = start / (1024 * 4096);
        uint32_t end_idx = (end - 1) / (1024 * 4096);
        for (uint32_t i = start_idx; i <= end_idx; i++) {
            uint32_t pde = task->page_directory[i];
            if (pde & PTE_PRESENT) {
                pmm_free_page((void*)(pde & ~0xFFF));
            }
        }
    }
    if (task->user_stack_base) {
        uint32_t pde_idx = task->user_stack_base / (1024 * 4096);
        int in_range = 0;
        if (task->user_pages && task->user_base) {
            uint32_t start = task->user_base;
            uint32_t end = task->user_base + task->user_pages * 4096;
            uint32_t start_idx = start / (1024 * 4096);
            uint32_t end_idx = (end - 1) / (1024 * 4096);
            if (pde_idx >= start_idx && pde_idx <= end_idx) {
                in_range = 1;
            }
        }
        if (!in_range) {
            uint32_t pde = task->page_directory[pde_idx];
            if (pde & PTE_PRESENT) {
                pmm_free_page((void*)(pde & ~0xFFF));
            }
        }
    }

    pmm_free_page(task->page_directory);
    vma_list_free(task);
}

static void task_destroy(task_t *prev, task_t *task)
{
    if (!prev || !task) return;
    if (!task_head || !task_current) return;
    if (task == task_current) return;

    uint32_t flags = irq_save();
    task_t *next = task->next;
    if (task == task_head) {
        task_head = next;
    }
    prev->next = next;
    irq_restore(flags);

    syscall_cleanup_task_fds(task->id);
    task_free_user_memory(task);
    kfree(task->stack);
    kfree(task);
}

void wait_queue_init(wait_queue_t *q)
{
    if (!q) return;
    q->head = NULL;
}

void wait_queue_block(wait_queue_t *q)
{
    if (!q) return;
    if (!task_current) return;
    uint32_t flags = irq_save();
    wait_queue_block_locked(q);
    irq_restore(flags);
}

void wait_queue_block_locked(wait_queue_t *q)
{
    if (!q) return;
    if (!task_current) return;
    if (task_current->waitq == q) return;
    if (task_current->state == TASK_ZOMBIE) return;

    task_current->waitq = q;
    task_current->wait_next = (task_t*)q->head;
    q->head = task_current;
    task_current->state = TASK_BLOCKED;
}

void wait_queue_wake_all(wait_queue_t *q)
{
    if (!q) return;
    uint32_t flags = irq_save();
    task_t *t = (task_t*)q->head;
    q->head = NULL;
    while (t) {
        task_t *next = t->wait_next;
        t->wait_next = NULL;
        t->waitq = NULL;
        if (t->state == TASK_BLOCKED) {
            t->state = TASK_RUNNABLE;
        }
        t = next;
    }
    irq_restore(flags);
}

void task_tick(void)
{
    uint32_t now = timer_get_ticks();
    task_t *t = task_head;
    if (!t) return;

    do {
        if (t->state == TASK_SLEEPING) {
            if ((int32_t)(now - t->wake_tick) >= 0) {
                t->state = TASK_RUNNABLE;
            }
        }
        t = t->next;
    } while (t && t != task_head);

    if (!task_current) return;
    if (task_current->state != TASK_RUNNABLE) {
        need_resched = 1;
        return;
    }

    task_current->timeslice--;
    if (task_current->timeslice <= 0) {
        need_resched = 1;
    }
}

registers_t *task_schedule(registers_t *regs)
{
    if (!task_current) return regs;

    task_current->regs = regs;
    if (task_current->state == TASK_RUNNABLE && !need_resched) {
        return task_current->regs;
    }

    need_resched = 0;
    task_current->timeslice = TASK_TIMESLICE_TICKS;

    task_t *candidate = task_current->next;
    while (candidate) {
        if (candidate->state == TASK_RUNNABLE) {
            if (candidate != task_current) {
                sched_switch_count++;
            }
            task_current = candidate;
            if (task_current->page_directory != vmm_get_current_directory()) {
                vmm_switch_directory(task_current->page_directory);
            }
            tss_set_kernel_stack((uint32_t)task_current->stack + 4096);
            task_current->timeslice = TASK_TIMESLICE_TICKS;
            return task_current->regs;
        }

        candidate = candidate->next;
        if (candidate == task_current) break;
    }

    return task_current->regs;
}

void task_sleep(uint32_t ticks)
{
    if (!task_current) return;
    if (ticks == 0) return;
    task_current->wake_tick = timer_get_ticks() + ticks;
    task_current->state = TASK_SLEEPING;
    need_resched = 1;
}

void task_yield(void)
{
    need_resched = 1;
    __asm__ volatile ("int $0x20");
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
    if (!task_current || !dir) return;
    task_current->page_directory = dir;
}

void task_set_user_info(uint32_t base, uint32_t pages, uint32_t stack_base)
{
    if (!task_current) return;
    task_current->user_base = base;
    task_current->user_pages = pages;
    task_current->user_stack_base = stack_base;
    if (!task_current->vma_list) {
        if (base && pages) {
            vma_add(task_current, base, base + pages * 4096, VMA_READ | VMA_WRITE | VMA_EXEC);
        }
        if (stack_base) {
            vma_add(task_current, stack_base, stack_base + 4096, VMA_READ | VMA_WRITE);
        }
    }
}

void task_get_user_info(uint32_t *base, uint32_t *pages, uint32_t *stack_base)
{
    if (base) *base = task_current ? task_current->user_base : 0;
    if (pages) *pages = task_current ? task_current->user_pages : 0;
    if (stack_base) *stack_base = task_current ? task_current->user_stack_base : 0;
}

int task_user_vma_allows(uint32_t addr, int is_write, int is_exec)
{
    if (!task_current) return 0;
    vma_t *v = task_current->vma_list;
    while (v) {
        if (addr >= v->start && addr < v->end) {
            if (is_exec && !(v->flags & VMA_EXEC)) return 0;
            if (is_write && !(v->flags & VMA_WRITE)) return 0;
            if (!is_write && !(v->flags & VMA_READ)) return 0;
            return 1;
        }
        v = v->next;
    }
    return 0;
}

void task_exit(void)
{
    if (!task_current) return;
    if (task_current->id == 0) return;
    task_exit_with_status(0);
}

void task_exit_with_status(int code)
{
    task_exit_with_reason(code, TASK_EXIT_NORMAL, 0, 0);
}

void task_exit_with_reason(int code, int reason, uint32_t info0, uint32_t info1)
{
    if (!task_current) return;
    if (task_current->id == 0) return;
    if (task_current->state == TASK_ZOMBIE) return;
    task_current->exit_code = code;
    task_current->exit_reason = reason;
    task_current->exit_info0 = info0;
    task_current->exit_info1 = info1;
    if (task_current->user_pages || task_current->user_stack_base) {
        shell_on_user_exit();
    }
    task_current->state = TASK_ZOMBIE;
    wait_queue_wake_all(&exit_waitq);
    task_yield();
}

int task_wait(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1)
{
    if (id == 0) return -1;
    if (!task_head || !task_current) return -1;
    if (task_current->id == id) return -1;

    for (;;) {
        uint32_t flags = irq_save();
        task_t *prev = task_head;
        task_t *t = task_head->next;
        while (t && t != task_head) {
            if (t->id == id) {
                if (t->state == TASK_ZOMBIE) {
                    irq_restore(flags);
                    if (out_code) *out_code = t->exit_code;
                    if (out_reason) *out_reason = t->exit_reason;
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
        task_yield();
    }
}

const char *task_get_current_program(void)
{
    if (!task_current) return NULL;
    if (!task_current->program[0]) return NULL;
    return task_current->program;
}

void task_user_heap_init(uint32_t heap_base, uint32_t stack_base)
{
    if (!task_current) return;
    if (heap_base < 0x1000) return;
    if (heap_base >= 0xC0000000) return;
    if (stack_base && heap_base + 0x1000 >= stack_base) return;
    task_current->heap_base = heap_base;
    task_current->heap_brk = heap_base;

    vma_t *heap = vma_find_heap(task_current);
    if (!heap) {
        vma_add(task_current, heap_base, heap_base, VMA_READ | VMA_WRITE);
    }
}

uint32_t task_brk(uint32_t new_end)
{
    if (!task_current) return 0;
    if (task_current->heap_base == 0) return 0;

    if (new_end == 0) {
        return task_current->heap_brk;
    }

    if (new_end < task_current->heap_base) {
        return task_current->heap_brk;
    }
    if (new_end >= 0xC0000000) {
        return task_current->heap_brk;
    }
    if (task_current->user_stack_base && align_up(new_end) + 0x1000 > task_current->user_stack_base) {
        return task_current->heap_brk;
    }

    task_current->heap_brk = new_end;

    vma_t *heap = vma_find_heap(task_current);
    if (heap) {
        uint32_t end = align_up(new_end);
        if (end < heap->start) end = heap->start;
        heap->end = end;
    }

    return task_current->heap_brk;
}

uint32_t task_get_current_id(void)
{
    if (!task_current) return 0;
    return task_current->id;
}

uint32_t task_get_switch_count(void)
{
    return sched_switch_count;
}

uint32_t task_dump_tasks(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    if (!task_head) {
        const char *s = "No tasks.\n";
        uint32_t n = (uint32_t)strlen(s);
        if (n > len) n = len;
        memcpy(buf, s, n);
        return n;
    }

    uint32_t off = 0;
    buf_append(buf, &off, len, "ID   STATE     WAKE    CURRENT  NAME\n");
    task_t *task = task_head;
    do {
        const char *state = "RUNNABLE";
        if (task->state == TASK_SLEEPING) state = "SLEEPING";
        else if (task->state == TASK_BLOCKED) state = "BLOCKED";
        else if (task->state == TASK_ZOMBIE) state = "ZOMBIE";

        const char *name = task->program[0] ? task->program : "-";
        buf_append_u32(buf, &off, len, task->id);
        buf_append(buf, &off, len, "    ");
        buf_append(buf, &off, len, state);
        buf_append(buf, &off, len, "  ");
        buf_append_u32(buf, &off, len, task->wake_tick);
        buf_append(buf, &off, len, "    ");
        buf_append(buf, &off, len, task == task_current ? "yes" : "no");
        buf_append(buf, &off, len, "     ");
        buf_append(buf, &off, len, name);
        buf_append(buf, &off, len, "\n");
        if (off >= len) break;
        task = task->next;
    } while (task && task != task_head);
    if (off < len) buf[off] = 0;
    return off;
}

uint32_t task_dump_maps(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    if (!task_current) return 0;

    uint32_t off = 0;
    vma_t *v = task_current->vma_list;
    while (v) {
        buf_append_hex(buf, &off, len, v->start);
        buf_append(buf, &off, len, "-");
        buf_append_hex(buf, &off, len, v->end);
        buf_append(buf, &off, len, " ");
        buf_append(buf, &off, len, (v->flags & VMA_READ) ? "r" : "-");
        buf_append(buf, &off, len, (v->flags & VMA_WRITE) ? "w" : "-");
        buf_append(buf, &off, len, (v->flags & VMA_EXEC) ? "x" : "-");
        buf_append(buf, &off, len, "\n");
        if (off >= len) break;
        v = v->next;
    }
    if (off < len) buf[off] = 0;
    return off;
}

void task_list(void)
{
    if (!task_head) {
        terminal_writestring("No tasks.\n");
        return;
    }

    terminal_writestring("ID   STATE     WAKE    CURRENT\n");
    task_t *task = task_head;
    do {
        const char *state = "RUNNABLE";
        if (task->state == TASK_SLEEPING) {
            state = "SLEEPING";
        } else if (task->state == TASK_BLOCKED) {
            state = "BLOCKED";
        } else if (task->state == TASK_ZOMBIE) {
            state = "ZOMBIE";
        }
        printf("%d    %s  %d    %s\n",
               task->id,
               state,
               task->wake_tick,
               task == task_current ? "yes" : "no");
        task = task->next;
    } while (task && task != task_head);
}
