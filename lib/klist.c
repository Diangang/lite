#include "linux/klist.h"

static struct klist *knode_klist(struct klist_node *n)
{
    return n ? (struct klist *)n->n_klist : NULL;
}

static void klist_node_init_internal(struct klist_node *n, struct klist *k)
{
    if (!n)
        return;
    INIT_LIST_HEAD(&n->n_node);
    kref_init(&n->n_ref);
    n->n_klist = k;
    if (k && k->get)
        k->get(n);
}

void klist_init(struct klist *k, void (*get)(struct klist_node *n), void (*put)(struct klist_node *n))
{
    if (!k)
        return;
    INIT_LIST_HEAD(&k->k_list);
    k->get = get;
    k->put = put;
}

void klist_add_head(struct klist_node *n, struct klist *k)
{
    if (!n || !k)
        return;
    klist_node_init_internal(n, k);
    list_add(&n->n_node, &k->k_list);
}

void klist_add_tail(struct klist_node *n, struct klist *k)
{
    if (!n || !k)
        return;
    klist_node_init_internal(n, k);
    list_add_tail(&n->n_node, &k->k_list);
}

void klist_del(struct klist_node *n)
{
    struct klist *k = knode_klist(n);

    if (!n || !k)
        return;
    if (n->n_node.next && n->n_node.prev && n->n_node.next != &n->n_node)
        list_del_init(&n->n_node);
    n->n_klist = NULL;
    if (k->put)
        k->put(n);
}

void klist_remove(struct klist_node *n)
{
    klist_del(n);
}

int klist_node_attached(struct klist_node *n)
{
    return n && n->n_klist;
}

void klist_iter_init(struct klist *k, struct klist_iter *i)
{
    if (!i)
        return;
    i->i_klist = k;
    i->i_cur = NULL;
}

void klist_iter_init_node(struct klist *k, struct klist_iter *i, struct klist_node *n)
{
    if (!i)
        return;
    i->i_klist = k;
    i->i_cur = n;
    if (n && k && k->get)
        k->get(n);
}

void klist_iter_exit(struct klist_iter *i)
{
    if (!i || !i->i_cur || !i->i_klist)
        return;
    if (i->i_klist->put)
        i->i_klist->put(i->i_cur);
    i->i_cur = NULL;
}

struct klist_node *klist_next(struct klist_iter *i)
{
    struct list_head *next;
    struct klist_node *n;

    if (!i || !i->i_klist)
        return NULL;

    if (i->i_cur) {
        next = i->i_cur->n_node.next;
        if (i->i_klist->put)
            i->i_klist->put(i->i_cur);
    } else {
        next = i->i_klist->k_list.next;
    }

    if (next == &i->i_klist->k_list) {
        i->i_cur = NULL;
        return NULL;
    }

    n = container_of(next, struct klist_node, n_node);
    if (i->i_klist->get)
        i->i_klist->get(n);
    i->i_cur = n;
    return n;
}
