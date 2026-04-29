#ifndef LINUX_WAIT_H
#define LINUX_WAIT_H

#include <stdint.h>
#include "linux/list.h"
#include "linux/spinlock.h"

struct task_struct;
typedef struct wait_queue_entry wait_queue_t;
typedef int (*wait_queue_func_t)(wait_queue_t *wait, unsigned mode, int flags, void *key);

#define WQ_FLAG_EXCLUSIVE 0x01
#define WQ_FLAG_WOKEN 0x02

typedef struct wait_queue_entry {
    unsigned int flags;
    void *private;
    wait_queue_func_t func;
    struct list_head task_list;
} wait_queue_entry_t;

typedef struct wait_queue_head {
    spinlock_t lock;
    struct list_head task_list;
} wait_queue_head_t;

#define __WAITQUEUE_INITIALIZER(name, tsk) { \
    .private = tsk, \
    .func = default_wake_function, \
    .task_list = { NULL, NULL } \
}

#define DECLARE_WAITQUEUE(name, tsk) \
    wait_queue_t name = __WAITQUEUE_INITIALIZER(name, tsk)

void __init_waitqueue_head(wait_queue_head_t *q);
void init_waitqueue_entry(wait_queue_entry_t *entry, struct task_struct *task);
int default_wake_function(wait_queue_t *wait, unsigned mode, int flags, void *key);
void add_wait_queue(wait_queue_head_t *q, wait_queue_t *wait);
void remove_wait_queue(wait_queue_head_t *q, wait_queue_t *wait);
void wait_queue_block(wait_queue_head_t *q);
void wait_queue_block_locked(wait_queue_head_t *q);
void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr, void *key);
void wait_queue_remove(wait_queue_head_t *q, struct task_struct *task);

static inline void init_waitqueue_head(wait_queue_head_t *q)
{
    __init_waitqueue_head(q);
}

static inline void wake_up_all(wait_queue_head_t *q)
{
    __wake_up(q, 0, 0, (void *)0);
}

static inline void wake_up(wait_queue_head_t *q)
{
    __wake_up(q, 0, 1, (void *)0);
}

static inline int waitqueue_active(wait_queue_head_t *q)
{
    return !list_empty(&q->task_list);
}

static inline void init_waitqueue_func_entry(wait_queue_t *q, wait_queue_func_t func)
{
    q->flags = 0;
    q->private = (void *)0;
    q->func = func;
}

int do_waitpid(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1);
int sys_waitpid(uint32_t id, void *status, uint32_t status_len, int from_user);

#endif
