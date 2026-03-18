#include "task.h"
#include "kheap.h"
#include "kernel.h"
#include "libc.h"
#include "timer.h"
#include "vmm.h"
#include "pmm.h"

typedef struct task {
    uint32_t id;
    registers_t *regs;
    uint32_t *stack;
    uint32_t wake_tick;
    int state;
    uint32_t* page_directory;
    uint32_t user_base;
    uint32_t user_pages;
    uint32_t user_stack_base;
    struct task *next;
} task_t;

static task_t *task_head = NULL;
static task_t *task_current = NULL;
static uint32_t next_task_id = 1;
static int demo_enabled = 0;

enum {
    TASK_RUNNABLE = 0,
    TASK_SLEEPING = 1,
    TASK_DEAD = 2
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
    task->regs = task_build_regs(stack, task_idle);
    task->stack = stack;
    task->wake_tick = 0;
    task->state = TASK_RUNNABLE;
    task->page_directory = vmm_get_current_directory();
    task->user_base = 0;
    task->user_pages = 0;
    task->user_stack_base = 0;
    task->next = task;

    task_head = task;
    task_current = task;
}

int task_create(void (*entry)(void))
{
    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    uint32_t *stack = (uint32_t*)kmalloc(4096);

    if (!task || !stack) {
        printf("TASK: Failed to create task.\n");
        return -1;
    }

    task->id = next_task_id++;
    task->regs = task_build_regs(stack, entry);
    task->stack = stack;
    task->wake_tick = 0;
    task->state = TASK_RUNNABLE;
    task->page_directory = vmm_get_current_directory();
    task->user_base = 0;
    task->user_pages = 0;
    task->user_stack_base = 0;

    task->next = task_head->next;
    task_head->next = task;

    return (int)task->id;
}

registers_t *task_schedule(registers_t *regs)
{
    if (!task_current) return regs;

    task_current->regs = regs;
    uint32_t now = timer_get_ticks();

    task_t *prev = task_current;
    task_t *candidate = task_current->next;
    while (candidate) {
        if (candidate->state == TASK_DEAD) {
            task_t *next = candidate->next;
            if (candidate == task_head) {
                task_head = next;
            }

            if (candidate->page_directory && candidate->page_directory != vmm_get_kernel_directory()) {
                if (candidate->user_pages && candidate->user_base) {
                    for (uint32_t i = 0; i < candidate->user_pages; i++) {
                        uint32_t va = candidate->user_base + i * 4096;
                        uint32_t phys = vmm_virt_to_phys_ex(candidate->page_directory, (void*)va);
                        if (phys != 0xFFFFFFFF) {
                            pmm_free_page((void*)(phys & ~0xFFF));
                        }
                    }
                }
                if (candidate->user_stack_base) {
                    uint32_t phys = vmm_virt_to_phys_ex(candidate->page_directory, (void*)candidate->user_stack_base);
                    if (phys != 0xFFFFFFFF) {
                        pmm_free_page((void*)(phys & ~0xFFF));
                    }
                }

                if (candidate->user_base) {
                    uint32_t pde_idx = candidate->user_base / (1024 * 4096);
                    uint32_t pde = candidate->page_directory[pde_idx];
                    if (pde & PTE_PRESENT) {
                        pmm_free_page((void*)(pde & ~0xFFF));
                    }
                }
                if (candidate->user_stack_base) {
                    uint32_t pde_idx = candidate->user_stack_base / (1024 * 4096);
                    uint32_t pde = candidate->page_directory[pde_idx];
                    if (pde & PTE_PRESENT) {
                        pmm_free_page((void*)(pde & ~0xFFF));
                    }
                }

                pmm_free_page(candidate->page_directory);
            }

            kfree(candidate->stack);
            kfree(candidate);
            prev->next = next;
            candidate = next;
            if (!candidate) break;
            continue;
        }

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
            return task_current->regs;
        }

        prev = candidate;
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

void task_exit(void)
{
    if (!task_current) return;
    if (task_current->id == 0) return;
    task_current->state = TASK_DEAD;
    task_yield();
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
        } else if (task->state == TASK_DEAD) {
            state = "DEAD";
        }
        printf("%d    %s  %d    %s\n",
               task->id,
               state,
               task->wake_tick,
               task == task_current ? "yes" : "no");
        task = task->next;
    } while (task && task != task_head);
}
