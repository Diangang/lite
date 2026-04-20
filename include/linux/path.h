#ifndef LINUX_PATH_H
#define LINUX_PATH_H

struct dentry;
struct vfsmount;

struct path {
    struct vfsmount *mnt;
    struct dentry *dentry;
};

#endif
