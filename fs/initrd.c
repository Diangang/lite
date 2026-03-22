#include "initrd.h"
#include "libc.h"
#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "console.h"

/* The InitRD image in memory */
struct initrd_file_header *file_headers;
struct vfs_inode *initrd_dev;
struct vfs_inode *root_nodes;
int nroot_nodes;

struct dirent dirent;

static uint32_t initrd_read(struct vfs_inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static struct dirent *initrd_readdir(struct vfs_inode *node, uint32_t index);
static struct vfs_inode *initrd_finddir(struct vfs_inode *node, const char *name);

static struct vfs_file_operations initrd_file_ops = {
    .read = initrd_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct vfs_file_operations initrd_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = initrd_readdir,
    .finddir = initrd_finddir,
    .ioctl = NULL
};

/* Read from a file in the InitRD */
static uint32_t initrd_read(struct vfs_inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    struct initrd_file_header header = file_headers[node->inode];
    if (offset > header.length)
        return 0;
    if (offset + size > header.length)
        size = header.length - offset;

    memcpy(buffer, (uint8_t*)(header.offset + offset), size);
    return size;
}

/* Read directory entry */
static struct dirent *initrd_readdir(struct vfs_inode *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(dirent.name, ".");
        dirent.ino = 0;
        return &dirent;
    }
    if (index == 1) {
        strcpy(dirent.name, "..");
        dirent.ino = 0;
        return &dirent;
    }
    
    if (index - 2 >= (uint32_t)nroot_nodes)
        return NULL;

    strcpy(dirent.name, file_headers[index - 2].name);
    dirent.name[strlen(file_headers[index - 2].name)] = 0;
    dirent.ino = root_nodes[index - 2].inode;
    return &dirent;
}

static struct vfs_inode *initrd_finddir(struct vfs_inode *node, const char *name)
{
    (void)node;
    if (!name) return NULL;
    
    // Some paths might include leading slashes when being resolved
    const char *search_name = name;
    while (*search_name == '/') search_name++;
    
    if (*search_name == 0) return node;

    // printf("DEBUG: initrd_finddir '%s'\n", search_name);

    for (int i = 0; i < nroot_nodes; i++) {
        // Handle names that might still have initrd/ prepended depending on resolution path
        const char *hdr_name = file_headers[i].name;
        
        // Remove trailing spaces or newlines if any
        char clean_hdr_name[64];
        strcpy(clean_hdr_name, hdr_name);
        for(int j = strlen(clean_hdr_name) - 1; j >= 0; j--) {
            if(clean_hdr_name[j] == ' ' || clean_hdr_name[j] == '\n' || clean_hdr_name[j] == '\r') {
                clean_hdr_name[j] = '\0';
            } else {
                break;
            }
        }
        
        // Direct match
        if (!strcmp(search_name, clean_hdr_name))
            return &root_nodes[i];
            
        // Match base name if search_name has path
        const char *search_base = search_name;
        const char *p = search_name;
        while (*p) {
            if (*p == '/') search_base = p + 1;
            p++;
        }
        if (!strcmp(search_base, clean_hdr_name))
            return &root_nodes[i];
            
        // Match base name if hdr_name has path
        const char *hdr_base = clean_hdr_name;
        p = clean_hdr_name;
        while (*p) {
            if (*p == '/') hdr_base = p + 1;
            p++;
        }
        if (!strcmp(search_base, hdr_base))
            return &root_nodes[i];
    }
    return NULL;
}

struct vfs_inode *init_initrd(struct multiboot_info* mbi)
{
    struct multiboot_module* mod = (struct multiboot_module*)mbi->mods_addr;
    uint32_t location = mod->mod_start;
    struct vfs_inode *initrd_root = NULL;

    if (location < 0x1000) {
        printf("DEBUG: ERROR: InitRD location is too low (NULL or IVT)!\n");
        return 0;
    }

    if (!vmm_is_mapped((void*)location)) {
        printf("DEBUG: ERROR: InitRD location is not mapped!\n");
        return 0;
    }

    /* The first 4 bytes contains the number of files */
    uint32_t *ptr = (uint32_t*)location;
    nroot_nodes = *ptr;

    if (nroot_nodes == 0 || nroot_nodes > 100) { // Arbitrary sanity check
        printf("DEBUG: ERROR: Invalid InitRD node count: %d\n", nroot_nodes);
        return 0;
    }

    /* Headers start right after the count */
    file_headers = (struct initrd_file_header*)(location + sizeof(uint32_t));

    /* Initialize the root directory node */
    initrd_root = (struct vfs_inode*)kmalloc(sizeof(struct vfs_inode));
    if (!initrd_root) {
        printf("DEBUG: kmalloc failed for initrd_root\n");
        return 0;
    }

    initrd_root->mask = 0555;
    initrd_root->uid = 0;
    initrd_root->gid = 0;
    initrd_root->inode = 0;
    initrd_root->length = 0;
    initrd_root->flags = FS_DIRECTORY;
    initrd_root->f_ops = &initrd_dir_ops;
    initrd_root->private_data = 0;
    initrd_root->impl = 0;

    /* Initialize file nodes */
    root_nodes = (struct vfs_inode*)kmalloc(sizeof(struct vfs_inode) * nroot_nodes);
    if (!root_nodes) {
        printf("DEBUG: kmalloc failed for root_nodes\n");
        return 0;
    }

    for (int i = 0; i < nroot_nodes; i++) {
        file_headers[i].offset += location; /* Adjust offset to be absolute */
        root_nodes[i].mask = 0444;
        root_nodes[i].uid = 0;
        root_nodes[i].gid = 0;
        root_nodes[i].length = file_headers[i].length;
        root_nodes[i].inode = i;
        root_nodes[i].flags = FS_FILE;
        root_nodes[i].f_ops = &initrd_file_ops;
        root_nodes[i].private_data = 0;
        root_nodes[i].impl = 0;
    }

    return initrd_root;
}
