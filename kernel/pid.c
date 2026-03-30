#include "linux/sched.h"
#include "linux/pid.h"
#include <stddef.h>

struct task_struct *find_task_by_pid(uint32_t pid)
{
    if (list_empty(&task_list_head))
        return NULL;
    struct task_struct *t;
    list_for_each_entry(t, &task_list_head, tasks) {
        if (t->pid == pid)
            return t;
    }
    return NULL;
}
