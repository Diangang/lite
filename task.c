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

typedef struct task {
    uint32_t id;
    uint32_t parent_id;
    registers_t *regs;
    uint32_t *stack;
    uint32_t wake_tick;
    int state;
    uint32_t* page_directory;
    uint32_t user_base;
    uint32_t user_pages;
    uint32_t user_stack_base;
    int exit_code;
    int exit_reason;
    uint32_t exit_info0;
    uint32_t exit_info1;
    char program[32];
    struct task *next;
} task_t;

static task_t *task_head = NULL;
static task_t *task_current = NULL;
static uint32_t next_task_id = 1;
static int demo_enabled = 0;

enum {
    TASK_RUNNABLE = 0,
    TASK_SLEEPING = 1,
    TASK_ZOMBIE = 2
};

static void task_idle(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
    }
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
    task->page_directory = vmm_get_current_directory();
    task->user_base = 0;
    task->user_pages = 0;
    task->user_stack_base = 0;
    task->exit_code = 0;
    task->exit_reason = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task->program[0] = 0;
    task->next = task;

    task_head = task;
    task_current = task;
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
    task->page_directory = vmm_get_current_directory();
    task->user_base = 0;
    task->user_pages = 0;
    task->user_stack_base = 0;
    task->exit_code = 0;
    task->exit_reason = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task_set_program_name(task, program);

    task->next = task_head->next;
    task_head->next = task;

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
}

static void task_destroy(task_t *prev, task_t *task)
{
    if (!prev || !task) return;
    if (!task_head || !task_current) return;
    if (task == task_current) return;

    task_t *next = task->next;
    if (task == task_head) {
        task_head = next;
    }
    prev->next = next;

    syscall_cleanup_task_fds(task->id);
    task_free_user_memory(task);
    kfree(task->stack);
    kfree(task);
}

registers_t *task_schedule(registers_t *regs)
{
    if (!task_current) return regs;

    task_current->regs = regs;
    uint32_t now = timer_get_ticks();

    task_t *candidate = task_current->next;
    while (candidate) {
        if (candidate->state == TASK_SLEEPING) {
            if ((int32_t)(now - candidate->wake_tick) >= 0) {
                candidate->state = TASK_RUNNABLE;
            }
        }

        if (candidate->state == TASK_RUNNABLE) {
            task_current = candidate;
            if (task_current->page_directory != vmm_get_current_directory()) {
                vmm_switch_directory(task_current->page_directory);
            }
            tss_set_kernel_stack((uint32_t)task_current->stack + 4096);
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
}

void task_yield(void)
{
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
}

void task_get_user_info(uint32_t *base, uint32_t *pages, uint32_t *stack_base)
{
    if (base) *base = task_current ? task_current->user_base : 0;
    if (pages) *pages = task_current ? task_current->user_pages : 0;
    if (stack_base) *stack_base = task_current ? task_current->user_stack_base : 0;
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
    task_yield();
}

int task_wait(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1)
{
    if (id == 0) return -1;
    if (!task_head || !task_current) return -1;
    if (task_current->id == id) return -1;

    for (;;) {
        task_t *prev = task_head;
        task_t *t = task_head->next;
        while (t && t != task_head) {
            if (t->id == id) {
                if (t->state == TASK_ZOMBIE) {
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
            return -1;
        }
        task_yield();
    }
}

const char *task_get_current_program(void)
{
    if (!task_current) return NULL;
    if (!task_current->program[0]) return NULL;
    return task_current->program;
}

uint32_t task_get_current_id(void)
{
    if (!task_current) return 0;
    return task_current->id;
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
