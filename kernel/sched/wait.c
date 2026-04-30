#include "linux/sched.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/uaccess.h"
#include "linux/wait.h"

void __init_waitqueue_head(wait_queue_head_t *q)
{
    if (!q)
        return;
    spin_lock_init(&q->lock);
    INIT_LIST_HEAD(&q->task_list);
}

void init_waitqueue_entry(wait_queue_entry_t *entry, struct task_struct *task)
{
    if (!entry)
        return;
    entry->flags = 0;
    entry->private = task;
    entry->func = default_wake_function;
    INIT_LIST_HEAD(&entry->task_list);
}

void wait_queue_block(wait_queue_head_t *q)
{
    wait_queue_block_locked(q);
}

int default_wake_function(wait_queue_t *wait, unsigned mode, int flags, void *key)
{
    struct task_struct *task;

    (void)mode;
    (void)flags;
    (void)key;
    if (!wait)
        return 0;
    task = (struct task_struct *)wait->private;
    if (!task)
        return 0;
    task->waitq = NULL;
    if (task->state == TASK_BLOCKED) {
        task->state = TASK_RUNNABLE;
        if (task->time_slice <= 0)
            task->time_slice = TASK_TIMESLICE_TICKS;
        return 1;
    }
    return 0;
}

void add_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
    if (!q || !wait)
        return;
    list_add(&wait->task_list, &q->task_list);
}

void add_wait_queue_exclusive(wait_queue_head_t *q, wait_queue_t *wait)
{
    unsigned long flags;

    if (!q || !wait)
        return;
    wait->flags |= WQ_FLAG_EXCLUSIVE;
    spin_lock_irqsave(&q->lock, flags);
    __add_wait_queue_tail(q, wait);
    spin_unlock_irqrestore(&q->lock, flags);
}

void remove_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
    if (!q || !wait)
        return;
    if (!list_empty(&wait->task_list))
        list_del_init(&wait->task_list);
}

void wait_queue_block_locked(wait_queue_head_t *q)
{
    struct task_struct *task = task_current();
    if (!q || !task)
        return;
    if (task->waitq == q)
        return;
    if (task->state == TASK_ZOMBIE)
        return;

    spin_lock(&q->lock);
    task->waitq = q;
    init_waitqueue_entry(&task->wait_entry, task);
    add_wait_queue(q, &task->wait_entry);
    task->state = TASK_BLOCKED;
    spin_unlock(&q->lock);
}

void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr, void *key)
{
    struct list_head wake_list;
    struct list_head *pos, *n;
    int woke = 0;

    (void)mode;
    (void)key;
    if (!q)
        return;
    INIT_LIST_HEAD(&wake_list);

    spin_lock(&q->lock);
    while (!list_empty(&q->task_list)) {
        struct list_head *entry = q->task_list.next;
        list_del_init(entry);
        list_add_tail(entry, &wake_list);
    }
    spin_unlock(&q->lock);

    list_for_each_safe(pos, n, &wake_list) {
        wait_queue_t *entry = list_entry(pos, wait_queue_t, task_list);
        list_del_init(&entry->task_list);
        woke |= entry->func ? entry->func(entry, mode, 0, key) : 0;
        if (nr > 0 && woke >= nr)
            break;
    }
    if (woke)
        task_set_need_resched();
}

void wait_queue_remove(wait_queue_head_t *q, struct task_struct *task)
{
    wait_queue_entry_t *entry;

    if (!q || !task)
        return;
    spin_lock(&q->lock);
    entry = &task->wait_entry;
    remove_wait_queue(q, entry);
    task->waitq = NULL;
    spin_unlock(&q->lock);
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
            if (!(want_any || task_pid_nr(t) == id))
                continue;
            has_target = 1;
            if (t->state == TASK_ZOMBIE) {
                /*
                 * Linux mapping: waitpid reaps an already-zombified child while
                 * holding tasklist_lock, then release_task() drops the final task
                 * list linkage afterwards.
                 */
                get_task_struct(t);
                list_del_init(&t->sibling);
                tasklist_unlock(flags);
                if (out_code) *out_code = t->exit_code;
                if (out_reason) *out_reason = t->exit_state;
                if (out_info0) *out_info0 = t->exit_info0;
                if (out_info1) *out_info1 = t->exit_info1;
                int waited = (int)task_pid_nr(t);
                release_task(t);
                put_task_struct(t);
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
