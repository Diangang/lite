#ifndef LINUX_KLIST_H
#define LINUX_KLIST_H

#include "list.h"
#include "kref.h"

struct klist_node;

struct klist {
    struct list_head k_list;
    void (*get)(struct klist_node *n);
    void (*put)(struct klist_node *n);
};

struct klist_node {
    void *n_klist;
    struct list_head n_node;
    struct kref n_ref;
};

struct klist_iter {
    struct klist *i_klist;
    struct klist_node *i_cur;
};

void klist_init(struct klist *k, void (*get)(struct klist_node *n), void (*put)(struct klist_node *n));
void klist_add_head(struct klist_node *n, struct klist *k);
void klist_add_tail(struct klist_node *n, struct klist *k);
void klist_del(struct klist_node *n);
void klist_remove(struct klist_node *n);
int klist_node_attached(struct klist_node *n);
void klist_iter_init(struct klist *k, struct klist_iter *i);
void klist_iter_init_node(struct klist *k, struct klist_iter *i, struct klist_node *n);
void klist_iter_exit(struct klist_iter *i);
struct klist_node *klist_next(struct klist_iter *i);

#endif
