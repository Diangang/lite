#ifndef LINUX_NAMEI_H
#define LINUX_NAMEI_H

#include "linux/path.h"
#include "linux/types.h"

#define AT_FDCWD (-100)

#define LOOKUP_FOLLOW    0x0001
#define LOOKUP_DIRECTORY 0x0002
#define LOOKUP_AUTOMOUNT 0x0004
#define LOOKUP_PARENT    0x0010
#define LOOKUP_REVAL     0x0020
#define LOOKUP_CREATE    0x0200
#define LOOKUP_ROOT      0x2000

enum {
    LAST_NORM = 0,
    LAST_ROOT,
    LAST_DOT,
    LAST_DOTDOT,
};

struct nameidata {
    struct path path;
    struct path root;
    const char *name;
    const char *last;
    unsigned flags;
    int last_type;
    int dfd;
    struct nameidata *saved;
};

int filename_lookup(int dfd, const char *name, unsigned flags, struct path *path);
int kern_path(const char *name, unsigned flags, struct path *path);
int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
                    const char *name, unsigned flags, struct path *path);
int filename_parentat(int dfd, const char *name, unsigned flags,
                      struct path *parent, char *last, uint32_t last_cap,
                      int *type);

#endif
