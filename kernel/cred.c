#include "linux/sched.h"
#include "linux/cred.h"

/* task_get_uid: Implement task get uid. */
uint32_t task_get_uid(void)
{
    if (!current)
        return 0;
    return current->uid;
}

/* task_get_gid: Implement task get gid. */
uint32_t task_get_gid(void)
{
    if (!current)
        return 0;
    return current->gid;
}

/* task_get_umask: Implement task get umask. */
uint32_t task_get_umask(void)
{
    if (!current)
        return 022;
    return current->umask;
}

/* task_set_umask: Implement task set umask. */
uint32_t task_set_umask(uint32_t mask)
{
    if (!current)
        return 022;
    uint32_t old = current->umask;
    current->umask = mask & 0777;
    return old;
}
