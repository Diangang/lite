#ifndef LINUX_IDR_H
#define LINUX_IDR_H

#include <stddef.h>

struct idr {
	void **slots;
	int size;
	int next_id;
};

void idr_init(struct idr *idp);
int idr_pre_get(struct idr *idp, unsigned int gfp_mask);
int idr_get_new(struct idr *idp, void *ptr, int *id);
int idr_get_new_above(struct idr *idp, void *ptr, int starting_id, int *id);
void *idr_find(struct idr *idp, int id);
void idr_remove(struct idr *idp, int id);

#endif
