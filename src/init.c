#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>

int main() {
    // 1. Disable stdout buffering for immediate serial console output
    setvbuf(stdout, NULL, _IONBF, 0);

    // 2. Create mount points and mount essential kernel filesystems
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    mkdir("/home", 0755);
    mkdir("/home/bishal", 0755);

    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    // 3. Set hostname
    sethostname("BishOS", 6);

    // 4. Set base environment variables
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("TERM", "linux", 1);

    // 5. Welcome banner
    printf("\n");
    printf("==========================================\n");
    printf("         Welcome to BishOS v0.2!          \n");
    printf("     Linux Kernel + BusyBox Userspace     \n");
    printf("==========================================\n");
    printf("\n");
    printf("Type 'whoami', 'id', 'll', or 'su - bishal'\n");
    printf("To cleanly shut down the OS, run: poweroff\n\n");

    // 6. PID 1 loop: launch login shell with controlling TTY
    while (1) {
        pid_t pid = fork();

        if (pid == 0) {
            // Create a new process session
            setsid();

            // Attach /dev/ttyS0 as the controlling terminal (TIOCSCTTY)
            int fd = open("/dev/ttyS0", O_RDWR);
            if (fd < 0) {
                fd = open("/dev/console", O_RDWR);
            }

            if (fd >= 0) {
                ioctl(fd, TIOCSCTTY, 1);
                dup2(fd, 0); // stdin
                dup2(fd, 1); // stdout
                dup2(fd, 2); // stderr
                if (fd > 2) {
                    close(fd);
                }
            }

            // Execute login shell (-l flag executes /etc/profile)
            char *argv[] = {"/bin/sh", "-l", NULL};
            execv("/bin/sh", argv);

            // Fallback
            char *bb_argv[] = {"/bin/busybox", "sh", "-l", NULL};
            execv("/bin/busybox", bb_argv);

            perror("[BishOS] Failed to execute shell");
            exit(1);
        } else if (pid > 0) {
            // Parent (PID 1): wait for the shell session to exit
            int status;
            waitpid(pid, &status, 0);
            printf("\n[BishOS] Shell session ended. Respawning shell...\n\n");
        } else {
            perror("[BishOS] fork failed");
            sleep(2);
        }
    }

    return 0;
}
