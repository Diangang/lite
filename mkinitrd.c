#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct initrd_header {
    uint32_t nfiles;
};

struct initrd_file_header {
    uint32_t magic;
    char name[64];
    uint32_t offset;
    uint32_t length;
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <file1> [file2] ...\n", argv[0]);
        return 1;
    }

    int nheaders = argc - 1;
    struct initrd_file_header headers[64];

    printf("Creating initrd with %d files\n", nheaders);

    unsigned int off = sizeof(struct initrd_file_header) * 64 + sizeof(uint32_t);

    for (int i = 0; i < nheaders; i++) {
        printf("Writing file %s at offset 0x%x\n", argv[i+1], off);
        strcpy(headers[i].name, argv[i+1]);
        headers[i].offset = off;
        headers[i].magic = 0xBF;

        FILE *stream = fopen(argv[i+1], "r");
        if (stream == 0) {
            printf("Error: File not found: %s\n", argv[i+1]);
            return 1;
        }
        fseek(stream, 0, SEEK_END);
        headers[i].length = ftell(stream);
        off += headers[i].length;
        fclose(stream);
        headers[i].magic = 0xBF;
    }

    FILE *wstream = fopen("initrd.img", "w");
    unsigned char *data = (unsigned char *)malloc(off);
    fwrite(&nheaders, sizeof(int), 1, wstream);
    fwrite(headers, sizeof(struct initrd_file_header), 64, wstream);

    for (int i = 0; i < nheaders; i++) {
        FILE *stream = fopen(argv[i+1], "r");
        unsigned char *buf = (unsigned char *)malloc(headers[i].length);
        fread(buf, 1, headers[i].length, stream);
        fwrite(buf, 1, headers[i].length, wstream);
        fclose(stream);
        free(buf);
    }

    fclose(wstream);
    free(data);

    return 0;
}