#ifndef LINUX_WAIT_H
#define LINUX_WAIT_H

typedef struct wait_queue {
    void *head;
} wait_queue_t;

void wait_queue_init(wait_queue_t *q);
void wait_queue_block(wait_queue_t *q);
void wait_queue_block_locked(wait_queue_t *q);
void wait_queue_wake_all(wait_queue_t *q);

#endif
