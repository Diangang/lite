#ifndef LINUX_IDR_H
#define LINUX_IDR_H

#include <stdbool.h>
#include "linux/page_alloc.h"
#include "linux/radix-tree.h"

struct idr {
	struct radix_tree_root root;
	int cur;
};

#define IDR_INIT(name) { .root = RADIX_TREE_INIT(GFP_KERNEL), .cur = 0 }
#define DEFINE_IDR(name) struct idr name = IDR_INIT(name)

void idr_init(struct idr *idp);
int idr_pre_get(struct idr *idp, unsigned int gfp_mask);
int idr_alloc(struct idr *idp, void *ptr, int start, int end, gfp_t gfp_mask);
int idr_get_new(struct idr *idp, void *ptr, int *id);
int idr_get_new_above(struct idr *idp, void *ptr, int starting_id, int *id);
void *idr_find(struct idr *idp, int id);
void idr_remove(struct idr *idp, int id);
void idr_destroy(struct idr *idp);
bool idr_is_empty(struct idr *idp);

#endif
