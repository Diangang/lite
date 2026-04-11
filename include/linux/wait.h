#ifndef LINUX_WAIT_H
#define LINUX_WAIT_H

#include <stdint.h>

struct task_struct;
struct wait_queue;

typedef struct wait_queue_entry {
    struct task_struct *task;
    struct wait_queue_entry *next;
} wait_queue_entry_t;

typedef struct wait_queue {
    wait_queue_entry_t *head;
} wait_queue_t;

typedef wait_queue_t wait_queue_head_t;

void wait_queue_init(wait_queue_t *q);
void init_waitqueue_entry(wait_queue_entry_t *entry, struct task_struct *task);
void wait_queue_block(wait_queue_t *q);
void wait_queue_block_locked(wait_queue_t *q);
void wait_queue_wake_all(wait_queue_t *q);
void wait_queue_remove(wait_queue_t *q, struct task_struct *task);

static inline void init_waitqueue_head(wait_queue_head_t *q)
{
    wait_queue_init(q);
}

static inline void wake_up_all(wait_queue_head_t *q)
{
    wait_queue_wake_all(q);
}

int do_waitpid(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1);
int sys_waitpid(uint32_t id, void *status, uint32_t status_len, int from_user);

#endif
