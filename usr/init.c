#include "ulib.h"

int main() {
    print("init: fork+exec /sbin/sh\n");

    while (1) {
        int pid = fork();
        if (pid == 0) {
            execve("/sbin/sh");
            print("init: execve failed!\n");
            exit(1);
        } else if (pid > 0) {
            int status[4];
            waitpid(pid, status, 16);
        } else {
            print("init: fork failed!\n");
            // If fork fails, sleep a bit before retrying
            for (volatile int i = 0; i < 1000000; i++);
        }
    }
    return 0;
}
