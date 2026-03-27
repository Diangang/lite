#ifndef LINUX_FS_STRUCT_H
#define LINUX_FS_STRUCT_H

struct dentry;

struct fs_struct {
    struct dentry *pwd;
    struct dentry *root;
};

#endif
