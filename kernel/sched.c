#include "task_internal.h"
#include "timer.h"
#include "vmm.h"
#include "gdt.h"

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
        __asm__ volatile("sti; hlt");
    }
}

int task_should_resched(void)
{
    if (!current)
        return 0;
    if (current->state != TASK_RUNNABLE)
        return 1;
    return need_resched != 0;
}

uint32_t task_get_switch_count(void)
{
    return sched_switch_count;
}
