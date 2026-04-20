#include "linux/exit.h"
#include "linux/irqflags.h"
#include "linux/libc.h"
#include "linux/sched.h"
#include "linux/uaccess.h"
#include "linux/wait.h"

void __init_waitqueue_head(wait_queue_head_t *q)
{
    if (!q)
        return;
    q->head = NULL;
}

void init_waitqueue_entry(wait_queue_entry_t *entry, struct task_struct *task)
{
    if (!entry)
        return;
    entry->task = task;
    entry->next = NULL;
}

void wait_queue_block(wait_queue_t *q)
{
    struct task_struct *task = task_current();
    if (!q || !task)
        return;
    uint32_t flags = irq_save();
    wait_queue_block_locked(q);
    irq_restore(flags);
}

void wait_queue_block_locked(wait_queue_t *q)
{
    struct task_struct *task = task_current();
    if (!q || !task)
        return;
    if (task->waitq == q)
        return;
    if (task->state == TASK_ZOMBIE)
        return;

    task->waitq = q;
    task->wait_entry.task = task;
    task->wait_entry.next = q->head;
    q->head = &task->wait_entry;
    task->state = TASK_BLOCKED;
}

void __wake_up(wait_queue_head_t *q)
{
    if (!q)
        return;

    uint32_t flags = irq_save();
    wait_queue_entry_t *entry = q->head;
    q->head = NULL;
    int woke = 0;
    while (entry) {
        wait_queue_entry_t *next = entry->next;
        struct task_struct *t = entry->task;
        entry->next = NULL;
        entry->task = t;
        if (!t) {
            entry = next;
            continue;
        }
        t->waitq = NULL;
        if (t->state == TASK_BLOCKED) {
            t->state = TASK_RUNNABLE;
            if (t->time_slice <= 0)
                t->time_slice = TASK_TIMESLICE_TICKS;
            woke = 1;
        }
        entry = next;
    }
    if (woke)
        task_set_need_resched();
    irq_restore(flags);
}

void wait_queue_remove(wait_queue_t *q, struct task_struct *task)
{
    if (!q || !task)
        return;

    uint32_t flags = irq_save();
    wait_queue_entry_t *prev = NULL;
    wait_queue_entry_t *entry = q->head;
    while (entry) {
        if (entry == &task->wait_entry) {
            if (prev)
                prev->next = entry->next;
            else
                q->head = entry->next;
            break;
        }
        prev = entry;
        entry = entry->next;
    }
    task->wait_entry.next = NULL;
    task->wait_entry.task = task;
    task->waitq = NULL;
    irq_restore(flags);
}

/* do_waitpid: Perform waitpid. */
int do_waitpid(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1)
{
    int want_any = (id == (uint32_t)-1);
    struct task_struct *task = task_current();
    if (id == 0)
        return -1;
    if (!task)
        return -1;
    if (!want_any && task->pid == id)
        return -1;

    for (;;) {
        uint32_t flags = tasklist_lock();
        int has_child = !list_empty(&task->children);
        int has_target = 0;
        struct task_struct *t;
        struct task_struct *n;
        list_for_each_entry_safe(t, n, &task->children, sibling) {
            if (!(want_any || t->pid == id))
                continue;
            has_target = 1;
            if (t->state == TASK_ZOMBIE) {
                /*
                 * Linux mapping: waitpid reaps an already-zombified child while
                 * holding tasklist_lock, then release_task() drops the final task
                 * list linkage afterwards.
                 */
                list_del_init(&t->sibling);
                t->parent = NULL;
                tasklist_unlock(flags);
                if (out_code) *out_code = t->exit_code;
                if (out_reason) *out_reason = t->exit_state;
                if (out_info0) *out_info0 = t->exit_info0;
                if (out_info1) *out_info1 = t->exit_info1;
                int waited = (int)t->pid;
                release_task(t);
                return waited;
            }
            if (!want_any)
                break;
        }
        if (!has_child || !has_target) {
            tasklist_unlock(flags);
            return -1;
        }
        wait_queue_block_locked(&task->child_exit_wait);
        tasklist_unlock(flags);
        task_yield();
    }
}

/* sys_waitpid: Implement sys waitpid. */
int sys_waitpid(uint32_t id, void *status, uint32_t status_len, int from_user)
{
    int code = 0;
    int reason = 0;
    uint32_t info0 = 0;
    uint32_t info1 = 0;
    int waited = do_waitpid(id, &code, &reason, &info0, &info1);
    if (waited < 0)
        return -1;

    if (status && status_len >= 16) {
        uint32_t tmp[4];
        tmp[0] = (uint32_t)code;
        tmp[1] = (uint32_t)reason;
        tmp[2] = info0;
        tmp[3] = info1;
        if (from_user) {
            if (copy_to_user(status, tmp, 16) != 0)
                return -1;
        } else {
            memcpy(status, tmp, 16);
        }
    }
    return waited;
}
