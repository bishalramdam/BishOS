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

    // 5. Load network driver and poll /sys/class/net until the kernel finishes PCIe registration
    system("insmod /lib/modules/e1000.ko 2>/dev/null || true");

    for (int i = 0; i < 30; i++) {
        if (access("/sys/class/net/eth0", F_OK) == 0) {
            break;
        }
        usleep(100000); // 100ms
    }

    // 6. Configure network interfaces & default gateway
    system("ifconfig lo 127.0.0.1 up");
    system("ifconfig eth0 10.0.2.15 netmask 255.255.255.0 broadcast 10.0.2.255 up 2>/dev/null || true");
    system("route add default gw 10.0.2.2 dev eth0 2>/dev/null || true");

    // 7. Welcome banner
    printf("\n");
    printf("==========================================\n");
    printf("         Welcome to BishOS v0.2!          \n");
    printf("     Linux Kernel + BusyBox + Network     \n");
    printf("==========================================\n");
    printf("\n");
    printf("Networking: Online (Intel e1000 + Google DNS: 8.8.8.8)\n");
    printf("Try: 'ping -c 3 8.8.8.8', 'nslookup google.com', 'wget -qO- http://icanhazip.com'\n");
    printf("To cleanly shut down the OS, run: poweroff\n\n");

    // 8. PID 1 loop: launch login shell with controlling TTY
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
