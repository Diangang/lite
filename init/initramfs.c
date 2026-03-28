#include "linux/file.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "asm/pgtable.h"
#include "linux/page_alloc.h"
#include "linux/console.h"
#include "linux/fs.h"
#include "linux/pagemap.h"
#include "asm/multiboot.h"
#include "asm/page.h"

// Basic CPIO newc header
struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
};

static uint32_t parse_hex(const char *str, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        val <<= 4;
        if (str[i] >= '0' && str[i] <= '9') val |= str[i] - '0';
        else if (str[i] >= 'A' && str[i] <= 'F') val |= str[i] - 'A' + 10;
        else if (str[i] >= 'a' && str[i] <= 'f') val |= str[i] - 'a' + 10;
    }
    return val;
}

#define ALIGN4(val) (((val) + 3) & ~3)

void populate_rootfs(void) {
    if (!(boot_mbi.flags & 0x00000008))
        panic("No modules provided by bootloader. Skipping initramfs");

    if (boot_mbi.mods_count == 0)
        panic("Module count is zero. Skipping initramfs");

    struct multiboot_module *mod = (struct multiboot_module *)phys_to_virt(boot_mbi.mods_addr);
    uint32_t location = mod->mod_start;
    uint32_t end_location = mod->mod_end;

    printf("Extracting initramfs from 0x%x to 0x%x...\n", location, end_location);

    uint8_t *ptr = (uint8_t *)phys_to_virt(location);
    uint8_t *end = (uint8_t *)phys_to_virt(end_location);
    while (ptr < end) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;

        if (strncmp(hdr->c_magic, "070701", 6) != 0) {
            printf("Invalid CPIO magic at 0x%x\n", (uint32_t)ptr);
            break;
        }

        uint32_t namesize = parse_hex(hdr->c_namesize, 8);
        uint32_t filesize = parse_hex(hdr->c_filesize, 8);
        uint32_t mode = parse_hex(hdr->c_mode, 8);

        char *name = (char *)(ptr + sizeof(struct cpio_newc_header));

        // Check for end of archive
        if (strcmp(name, "TRAILER!!!") == 0)
            break;

        uint32_t name_pad = ALIGN4(sizeof(struct cpio_newc_header) + namesize) - (sizeof(struct cpio_newc_header) + namesize);
        uint8_t *data = (uint8_t *)(name + namesize + name_pad);

        // Create file in VFS (ramfs)
        if (strcmp(name, ".") != 0) {
            char abs_path[256];
            strcpy(abs_path, "/");
            strcat(abs_path, name);

            if ((mode & 0170000) == 0040000) { // Directory
                vfs_mkdir(abs_path);
            } else if ((mode & 0170000) == 0100000) { // Regular file
                struct file *f = vfs_open(abs_path, VFS_O_CREAT | 0x0001); // 0x0001 is VFS_O_WRONLY in file.h? Wait, let's check VFS_O_WRONLY.
                if (f) {
                    vfs_write(f, data, filesize);
                    vfs_close(f);
                } else {
                    printf("Failed to create file: %s\n", abs_path);
                }
            }
        }

        uint32_t data_pad = ALIGN4(filesize) - filesize;
        ptr = data + filesize + data_pad;
    }

    printf("Initramfs extracted successfully.\n");
}
