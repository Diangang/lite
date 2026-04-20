#include "linux/sched.h"
#include "linux/cred.h"

/* current_uid: Return the current task uid. */
uint32_t current_uid(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    return task->uid;
}

/* current_gid: Return the current task gid. */
uint32_t current_gid(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    return task->gid;
}

/* current_umask: Return the current task umask. */
uint32_t current_umask(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 022;
    return task->umask;
}

/* sys_umask: Set the current task umask and return the previous value. */
uint32_t sys_umask(uint32_t mask)
{
    struct task_struct *task = task_current();
    if (!task)
        return 022;
    uint32_t old = task->umask;
    task->umask = mask & 0777;
    return old;
}
