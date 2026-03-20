#include "initrd.h"
#include "libc.h"
#include "kheap.h"
#include "vmm.h"
#include "pmm.h"

void serial_put_str(const char* data);
void serial_put_char(char a);

/* The InitRD image in memory */
initrd_file_header_t *file_headers;
struct fs_node *initrd_root;
struct fs_node *initrd_dev;
struct fs_node *root_nodes;
int nroot_nodes;

struct dirent dirent;

/* Read from a file in the InitRD */
static uint32_t initrd_read(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    initrd_file_header_t header = file_headers[node->inode];
    if (offset > header.length)
        return 0;
    if (offset + size > header.length)
        size = header.length - offset;

    memcpy(buffer, (uint8_t*)(header.offset + offset), size);
    return size;
}

/* Read directory entry */
static struct dirent *initrd_readdir(struct fs_node *node, uint32_t index)
{
    (void)node;
    if (index >= (uint32_t)nroot_nodes)
        return NULL;

    strcpy(dirent.name, root_nodes[index].name);
    dirent.name[strlen(root_nodes[index].name)] = 0;
    dirent.ino = root_nodes[index].inode;
    return &dirent;
}

static struct fs_node *initrd_finddir(struct fs_node *node, char *name)
{
    (void)node;
    for (int i = 0; i < nroot_nodes; i++)
        if (!strcmp(name, root_nodes[i].name))
            return &root_nodes[i];
    return NULL;
}

struct fs_node *init_initrd(uint32_t location)
{
    if (location < 0x1000) {
        serial_put_str("DEBUG: ERROR: InitRD location is too low (NULL or IVT)!\n");
        return 0;
    }

    if (!vmm_is_mapped((void*)location)) {
        serial_put_str("DEBUG: ERROR: InitRD location is not mapped!\n");
        return 0;
    }

    /* The first 4 bytes contains the number of files */
    uint32_t *ptr = (uint32_t*)location;

    nroot_nodes = *ptr;

    if (nroot_nodes > 100) {
        serial_put_str("DEBUG: nroot_nodes looks suspicious! Possible garbage data.\n");
        return 0;
    }

    /* Headers start right after the count */
    file_headers = (initrd_file_header_t*)(location + sizeof(uint32_t));

    /* Initialize the root directory node */
    initrd_root = (struct fs_node*)kmalloc(sizeof(struct fs_node));
    if (!initrd_root) {
        serial_put_str("DEBUG: kmalloc failed for initrd_root\n");
        return 0;
    }

    strcpy(initrd_root->name, "initrd");
    initrd_root->mask = 0555;
    initrd_root->uid = 0;
    initrd_root->gid = 0;
    initrd_root->inode = 0;
    initrd_root->length = 0;
    initrd_root->flags = FS_DIRECTORY;
    initrd_root->read = 0;
    initrd_root->write = 0;
    initrd_root->open = 0;
    initrd_root->close = 0;
    initrd_root->readdir = &initrd_readdir;
    initrd_root->finddir = &initrd_finddir;
    initrd_root->ptr = 0;
    initrd_root->impl = 0;

    /* Initialize file nodes */
    root_nodes = (struct fs_node*)kmalloc(sizeof(struct fs_node) * nroot_nodes);
    if (!root_nodes) {
        serial_put_str("DEBUG: kmalloc failed for root_nodes\n");
        return 0;
    }

    for (int i = 0; i < nroot_nodes; i++) {
        file_headers[i].offset += location; /* Adjust offset to be absolute */
        strcpy(root_nodes[i].name, file_headers[i].name);
        root_nodes[i].mask = 0444;
        root_nodes[i].uid = 0;
        root_nodes[i].gid = 0;
        root_nodes[i].length = file_headers[i].length;
        root_nodes[i].inode = i;
        root_nodes[i].flags = FS_FILE;
        root_nodes[i].read = &initrd_read;
        root_nodes[i].write = 0;
        root_nodes[i].readdir = 0;
        root_nodes[i].finddir = 0;
        root_nodes[i].open = 0;
        root_nodes[i].close = 0;
        root_nodes[i].impl = 0;
    }

    return initrd_root;
}
