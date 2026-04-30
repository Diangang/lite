#include "linux/sched.h"
#include "linux/pid.h"
#include <stddef.h>

/* find_task_by_vpid: Find task by virtual pid. */
struct task_struct *find_task_by_vpid(uint32_t pid)
{
    if (list_empty(&task_list_head))
        return NULL;
    struct task_struct *t;
    list_for_each_entry(t, &task_list_head, tasks) {
        if (task_pid_nr(t) == pid)
            return t;
    }
    return NULL;
}
