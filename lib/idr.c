#include "../include/linux/idr.h"
#include "../include/linux/slab.h"

void idr_init(struct idr *idp)
{
	idp->slots = NULL;
	idp->size = 0;
	idp->next_id = 0;
}

int idr_pre_get(struct idr *idp, unsigned int gfp_mask)
{
	(void)idp;
	(void)gfp_mask;
	return 1;
}

static int idr_expand(struct idr *idp, int new_size)
{
	void **new_slots = kmalloc(sizeof(void *) * (size_t)new_size);
	if (!new_slots)
		return -1;
	for (int i = 0; i < new_size; i++)
		new_slots[i] = NULL;
	for (int i = 0; i < idp->size; i++)
		new_slots[i] = idp->slots[i];
	kfree(idp->slots);
	idp->slots = new_slots;
	idp->size = new_size;
	return 0;
}

int idr_get_new_above(struct idr *idp, void *ptr, int starting_id, int *id)
{
	if (starting_id < 0)
		starting_id = 0;
	if (idp->size == 0 && idr_expand(idp, 32) != 0)
		return -1;
	if (starting_id >= idp->size && idr_expand(idp, starting_id + 32) != 0)
		return -1;

	for (int i = starting_id; i < idp->size; i++) {
		if (!idp->slots[i]) {
			idp->slots[i] = ptr;
			*id = i;
			if (i >= idp->next_id)
				idp->next_id = i + 1;
			return 0;
		}
	}

	if (idr_expand(idp, idp->size + 32) != 0)
		return -1;

	for (int i = starting_id; i < idp->size; i++) {
		if (!idp->slots[i]) {
			idp->slots[i] = ptr;
			*id = i;
			if (i >= idp->next_id)
				idp->next_id = i + 1;
			return 0;
		}
	}

	return -1;
}

int idr_get_new(struct idr *idp, void *ptr, int *id)
{
	int start = idp->next_id;
	if (idr_get_new_above(idp, ptr, start, id) == 0)
		return 0;
	return idr_get_new_above(idp, ptr, 0, id);
}

void *idr_find(struct idr *idp, int id)
{
	if (id < 0 || id >= idp->size)
		return NULL;
	return idp->slots[id];
}

void idr_remove(struct idr *idp, int id)
{
	if (id < 0 || id >= idp->size)
		return;
	idp->slots[id] = NULL;
	if (id < idp->next_id)
		idp->next_id = id;
}
