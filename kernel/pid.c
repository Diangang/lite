#include "linux/sched.h"
#include "linux/pid.h"
#include <stddef.h>

struct task_struct *find_task_by_pid(uint32_t pid)
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
