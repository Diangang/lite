#include "linux/sched.h"
#include "linux/exit.h"
#include "linux/pid.h"
#include "linux/kheap.h"
#include "linux/libc.h"
#include "linux/irqflags.h"
#include "linux/uaccess.h"

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

    files_close_all(task);
    task_free_user_memory(task);
    if (task->thread.sp0)
        kfree((void*)((uint32_t)task->thread.sp0 - THREAD_SIZE));
    kfree(task);
}

void do_exit(int code)
{
    if (!current)
        return;
    if (current->pid == 0)
        return;
    do_exit_reason(code, TASK_EXIT_NORMAL, 0, 0);
}

void do_exit_reason(int code, int reason, uint32_t info0, uint32_t info1)
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
    return;
}

void sys_exit(int code)
{
    do_exit(code);
}

int sys_waitpid(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1)
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
        task_yield();
    }
}

int sys_waitpid_uapi(uint32_t id, void *status, uint32_t status_len, int from_user)
{
    int code = 0;
    int reason = 0;
    uint32_t info0 = 0;
    uint32_t info1 = 0;
    if (sys_waitpid(id, &code, &reason, &info0, &info1) != 0)
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
    return 0;
}

int sys_kill(uint32_t id, int sig)
{
    if (id == 0)
        return -1;
    if (!task_head)
        return -1;
    uint32_t flags = irq_save();
    struct task_struct *t = find_task_by_pid(id);
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
        do_exit_reason(128 + sig, TASK_EXIT_SIGNAL, (uint32_t)sig, 0);
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
