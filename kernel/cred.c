#include "linux/sched.h"
#include "linux/cred.h"

/* task_get_uid: Implement task get uid. */
uint32_t task_get_uid(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    return task->uid;
}

/* task_get_gid: Implement task get gid. */
uint32_t task_get_gid(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    return task->gid;
}

/* task_get_umask: Implement task get umask. */
uint32_t task_get_umask(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 022;
    return task->umask;
}

/* task_set_umask: Implement task set umask. */
uint32_t task_set_umask(uint32_t mask)
{
    struct task_struct *task = task_current();
    if (!task)
        return 022;
    uint32_t old = task->umask;
    task->umask = mask & 0777;
    return old;
}
