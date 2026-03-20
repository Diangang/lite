#include "ramfs.h"
#include "kheap.h"
#include "libc.h"
#include "task.h"

typedef struct ramfs_node {
    uint32_t magic;
    struct ramfs_node *parent;
    struct ramfs_node *children;
    struct ramfs_node *next;
    uint8_t *data;
    uint32_t cap;
    struct fs_node node;
} ramfs_node_t;

enum { RAMFS_MAGIC = 0x52414D46 };

static uint32_t ramfs_next_inode = 0x9000;
static struct dirent ramfs_dirent;

static uint32_t ramfs_apply_umask(uint32_t mode)
{
    uint32_t mask = task_get_umask();
    return mode & (~mask) & 0777;
}

static ramfs_node_t *ramfs_get(struct fs_node *n)
{
    if (!n) return NULL;
    ramfs_node_t *rn = (ramfs_node_t*)(uintptr_t)n->impl;
    if (!rn || rn->magic != RAMFS_MAGIC) return NULL;
    return rn;
}

static uint32_t ramfs_read(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    ramfs_node_t *rn = ramfs_get(node);
    if (!rn || !buffer || size == 0) return 0;
    if ((node->flags & 0x7) != FS_FILE) return 0;
    if (offset >= node->length) return 0;
    uint32_t remain = node->length - offset;
    if (size > remain) size = remain;
    memcpy(buffer, rn->data + offset, size);
    return size;
}

static uint32_t ramfs_write(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    ramfs_node_t *rn = ramfs_get(node);
    if (!rn || !buffer || size == 0) return 0;
    if ((node->flags & 0x7) != FS_FILE) return 0;
    uint32_t end = offset + size;
    if (end < offset) return 0;

    if (end > rn->cap) {
        uint32_t new_cap = rn->cap ? rn->cap : 64;
        while (new_cap < end) {
            uint32_t next = new_cap * 2;
            if (next < new_cap) {
                new_cap = end;
                break;
            }
            new_cap = next;
        }
        uint8_t *nd = (uint8_t*)kmalloc(new_cap);
        if (!nd) return 0;
        if (rn->data && rn->cap) {
            memcpy(nd, rn->data, rn->cap);
            kfree(rn->data);
        }
        rn->data = nd;
        rn->cap = new_cap;
    }

    if (offset > node->length) {
        memset(rn->data + node->length, 0, offset - node->length);
    }
    memcpy(rn->data + offset, buffer, size);
    if (end > node->length) node->length = end;
    return size;
}

static struct dirent *ramfs_readdir(struct fs_node *node, uint32_t index)
{
    ramfs_node_t *rn = ramfs_get(node);
    if (!rn) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY) return NULL;
    ramfs_node_t *c = rn->children;
    while (c && index) {
        c = c->next;
        index--;
    }
    if (!c) return NULL;
    strcpy(ramfs_dirent.name, c->node.name);
    ramfs_dirent.ino = c->node.inode;
    return &ramfs_dirent;
}

static struct fs_node *ramfs_finddir(struct fs_node *node, char *name)
{
    ramfs_node_t *rn = ramfs_get(node);
    if (!rn || !name) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY) return NULL;
    for (ramfs_node_t *c = rn->children; c; c = c->next) {
        if (!strcmp(c->node.name, name)) return &c->node;
    }
    return NULL;
}

static int ramfs_valid_name(const char *name)
{
    if (!name || !*name) return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '/') return 0;
    }
    return 1;
}

struct fs_node *ramfs_create_child(struct fs_node *dir, const char *name, uint32_t type)
{
    if (!dir || !name) return NULL;
    if (!ramfs_valid_name(name)) return NULL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return NULL;
    ramfs_node_t *parent = ramfs_get(dir);
    if (!parent) return NULL;

    if (ramfs_finddir(dir, (char*)name)) return NULL;

    ramfs_node_t *rn = (ramfs_node_t*)kmalloc(sizeof(ramfs_node_t));
    if (!rn) return NULL;
    memset(rn, 0, sizeof(*rn));
    rn->magic = RAMFS_MAGIC;
    rn->parent = parent;
    rn->children = NULL;
    rn->next = parent->children;
    parent->children = rn;

    memset(&rn->node, 0, sizeof(rn->node));
    uint32_t name_len = (uint32_t)strlen(name);
    if (name_len >= sizeof(rn->node.name)) name_len = sizeof(rn->node.name) - 1;
    memcpy(rn->node.name, name, name_len);
    rn->node.name[name_len] = 0;
    rn->node.inode = ramfs_next_inode++;
    rn->node.impl = (uint32_t)(uintptr_t)rn;
    rn->node.uid = task_get_uid();
    rn->node.gid = task_get_gid();

    if (type == FS_DIRECTORY) {
        rn->node.flags = FS_DIRECTORY;
        rn->node.readdir = &ramfs_readdir;
        rn->node.finddir = &ramfs_finddir;
        rn->node.mask = ramfs_apply_umask(0777);
    } else {
        rn->node.flags = FS_FILE;
        rn->node.read = &ramfs_read;
        rn->node.write = &ramfs_write;
        rn->node.length = 0;
        rn->node.mask = ramfs_apply_umask(0666);
    }
    return &rn->node;
}

struct fs_node *ramfs_init(void)
{
    ramfs_node_t *rn = (ramfs_node_t*)kmalloc(sizeof(ramfs_node_t));
    if (!rn) return NULL;
    memset(rn, 0, sizeof(*rn));
    rn->magic = RAMFS_MAGIC;
    rn->parent = NULL;
    rn->children = NULL;
    rn->next = NULL;
    rn->data = NULL;
    rn->cap = 0;

    memset(&rn->node, 0, sizeof(rn->node));
    strcpy(rn->node.name, "/");
    rn->node.flags = FS_DIRECTORY;
    rn->node.inode = ramfs_next_inode++;
    rn->node.impl = (uint32_t)(uintptr_t)rn;
    rn->node.readdir = &ramfs_readdir;
    rn->node.finddir = &ramfs_finddir;
    rn->node.uid = 0;
    rn->node.gid = 0;
    rn->node.mask = 0755;
    return &rn->node;
}
