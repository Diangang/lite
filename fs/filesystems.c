#include "linux/fs.h"
#include "linux/string.h"

/*
 * Linux mapping: linux2.6/fs/filesystems.c
 *
 * Lite subset: a simple singly-linked list of file_system_type without locking
 * (Lite is single-core and does not unload filesystem modules).
 */

static struct file_system_type *file_systems;

struct file_system_type *get_fs_type(const char *name)
{
    if (!name)
        return NULL;
    for (struct file_system_type *fs = file_systems; fs; fs = fs->next) {
        if (strcmp(fs->name, name) == 0)
            return fs;
    }
    return NULL;
}

int register_filesystem(struct file_system_type *fs)
{
    struct file_system_type **p;
    if (!fs)
        return -1;

    for (p = &file_systems; *p; p = &(*p)->next) {
        if (strcmp((*p)->name, fs->name) == 0)
            return -1;
    }

    fs->next = NULL;
    *p = fs;
    return 0;
}

int unregister_filesystem(struct file_system_type *fs)
{
    struct file_system_type **p;
    if (!fs)
        return -1;

    for (p = &file_systems; *p; p = &(*p)->next) {
        if (*p == fs) {
            *p = fs->next;
            return 0;
        }
    }
    return -1;
}

