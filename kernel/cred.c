#include "linux/sched.h"
#include "linux/cred.h"

uint32_t task_get_uid(void)
{
    if (!current)
        return 0;
    return current->uid;
}

uint32_t task_get_gid(void)
{
    if (!current)
        return 0;
    return current->gid;
}

uint32_t task_get_umask(void)
{
    if (!current)
        return 022;
    return current->umask;
}

uint32_t task_set_umask(uint32_t mask)
{
    if (!current)
        return 022;
    uint32_t old = current->umask;
    current->umask = mask & 0777;
    return old;
}
