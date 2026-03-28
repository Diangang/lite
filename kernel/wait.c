#include "linux/exit.h"
#include "linux/irqflags.h"
#include "linux/libc.h"
#include "linux/sched.h"
#include "linux/uaccess.h"
#include "linux/wait.h"

int do_waitpid(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1)
{
    int want_any = (id == (uint32_t)-1);
    if (id == 0)
        return -1;
    if (!task_head || !current)
        return -1;
    if (!want_any && current->pid == id)
        return -1;

    for (;;) {
        uint32_t flags = irq_save();
        struct task_struct *prev = task_head;
        struct task_struct *t = task_head->next;
        int has_child = 0;
        int has_target = 0;
        while (t && t != task_head) {
            if (t->parent == current) {
                has_child = 1;
                if (want_any || t->pid == id) {
                    has_target = 1;
                    if (t->state == TASK_ZOMBIE) {
                        irq_restore(flags);
                        if (out_code) *out_code = t->exit_code;
                        if (out_reason) *out_reason = t->exit_state;
                        if (out_info0) *out_info0 = t->exit_info0;
                        if (out_info1) *out_info1 = t->exit_info1;
                        int waited = (int)t->pid;
                        task_destroy(prev, t);
                        return waited;
                    }
                    if (!want_any)
                        break;
                }
            }
            prev = t;
            t = t->next;
        }
        if (!has_child || !has_target) {
            irq_restore(flags);
            return -1;
        }
        wait_queue_block_locked(&exit_waitq);
        irq_restore(flags);
        task_yield();
    }
}

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
