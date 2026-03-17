#include "task.h"
#include "kheap.h"
#include "kernel.h"
#include "libc.h"
#include "timer.h"

typedef struct task {
    uint32_t id;
    registers_t *regs;
    uint32_t *stack;
    uint32_t wake_tick;
    int state;
    struct task *next;
} task_t;

static task_t *task_head = NULL;
static task_t *task_current = NULL;
static uint32_t next_task_id = 1;
static int demo_enabled = 0;

enum {
    TASK_RUNNABLE = 0,
    TASK_SLEEPING = 1
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

    task->next = task_head->next;
    task_head->next = task;

    return (int)task->id;
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

void task_list(void)
{
    if (!task_head) {
        terminal_writestring("No tasks.\n");
        return;
    }

    terminal_writestring("ID   STATE     WAKE    CURRENT\n");
    task_t *task = task_head;
    do {
        const char *state = task->state == TASK_SLEEPING ? "SLEEPING" : "RUNNABLE";
        printf("%d    %s  %d    %s\n",
               task->id,
               state,
               task->wake_tick,
               task == task_current ? "yes" : "no");
        task = task->next;
    } while (task && task != task_head);
}
