#include "ulib.h"

// Basic strcmp
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// Basic strncmp
int strncmp(const char *s1, const char *s2, int n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int strlen(const char *s) {
    int len = 0;
    while(s[len]) len++;
    return len;
}

void print_char(char c) {
    write(1, &c, 1);
}

void skip_spaces(char **p) {
    while (**p == ' ') (*p)++;
}

char* split_token(char **p) {
    skip_spaces(p);
    char *token = *p;
    if (*token == '\0') return 0;
    
    while (**p && **p != ' ') {
        (*p)++;
    }
    
    if (**p == ' ') {
        **p = '\0';
        (*p)++;
    }
    return token;
}

struct linux_dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[];
};

void do_ls(char *path) {
    if (!path || *path == '\0') path = ".";
    
    int fd = open(path, 0);
    if (fd < 0) {
        print("ls: open failed\n");
        return;
    }
    
    char buf[256];
    while (1) {
        int nread = syscall3(21, fd, (int)buf, sizeof(buf)); // SYS_GETDENTS = 21
        if (nread <= 0) break;
        
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent *d = (struct linux_dirent *)(buf + bpos);
            print(d->d_name);
            print("\n");
            bpos += d->d_reclen;
        }
    }
    close(fd);
}

void do_cat(char *path) {
    if (!path || *path == '\0') {
        print("cat: missing path\n");
        return;
    }
    
    int fd = open(path, 0);
    if (fd < 0) {
        print("cat: open failed\n");
        return;
    }
    
    char buf[256];
    while (1) {
        int nread = read(fd, buf, sizeof(buf));
        if (nread <= 0) break;
        write(1, buf, nread);
    }
    close(fd);
}

void do_writefile(char *path, char *content) {
    if (!path || !content) {
        print("writefile: missing args\n");
        return;
    }
    
    int fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        print("writefile: open failed\n");
        return;
    }
    
    write(fd, content, strlen(content));
    write(fd, "\n", 1);
    close(fd);
}

void do_run(char *path) {
    if (!path || *path == '\0') {
        print("run: missing path\n");
        return;
    }
    
    int pid = fork();
    if (pid == 0) {
        execve(path);
        print("run: execve failed\n");
        exit(1);
    } else if (pid > 0) {
        int status[4];
        waitpid(pid, status, 16);
    } else {
        print("run: fork failed\n");
    }
}

int main() {
    print("ush: user shell\n");
    char linebuf[256];
    
    while (1) {
        print("ush$ ");
        int pos = 0;
        char c;
        
        while (1) {
            int n = read(0, &c, 1);
            if (n <= 0) continue;
            
            if (c == '\r' || c == '\n') {
                linebuf[pos] = '\0';
                // TTY driver already echoes '\n', we don't need to print it again here
                // print("\n");
                break;
            } else if (c == '\b' || c == 127) {
                if (pos > 0) {
                    pos--;
                    // TTY driver already handles backspace echo
                    // print("\b \b");
                }
            } else if (pos < 255) {
                linebuf[pos++] = c;
                // TTY driver already echoes characters, we don't need to print it here
                // print_char(c);
            }
        }
        
        char *p = linebuf;
        char *cmd = split_token(&p);
        if (!cmd) continue;
        
        if (strcmp(cmd, "pwd") == 0) {
            char cwd[128];
            int ret = syscall2(11, (int)cwd, sizeof(cwd)); // SYS_GETCWD
            if (ret >= 0) {
                print(cwd);
                print("\n");
            }
        } else if (strcmp(cmd, "cd") == 0) {
            char *dir = split_token(&p);
            if (!dir) dir = "/";
            syscall1(10, (int)dir); // SYS_CHDIR
        } else if (strcmp(cmd, "ls") == 0) {
            do_ls(split_token(&p));
        } else if (strcmp(cmd, "cat") == 0) {
            do_cat(split_token(&p));
        } else if (strcmp(cmd, "mkdir") == 0) {
            char *dir = split_token(&p);
            if (dir) syscall1(13, (int)dir); // SYS_MKDIR
        } else if (strcmp(cmd, "writefile") == 0) {
            char *file = split_token(&p);
            skip_spaces(&p); // The rest is content
            do_writefile(file, p);
        } else if (strcmp(cmd, "run") == 0) {
            do_run(split_token(&p));
        } else if (strcmp(cmd, "exit") == 0) {
            exit(0);
        } else {
            print("unknown\n");
        }
    }
    
    return 0;
}
