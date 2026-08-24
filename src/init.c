#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>

int main() {
    // 1. Disable stdout buffering for immediate serial console output
    setvbuf(stdout, NULL, _IONBF, 0);

    // 2. Create mount points and mount essential kernel filesystems
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    mkdir("/home", 0755);
    mkdir("/home/bishal", 0755);

    // Give user bishal (UID 1000, GID 1000) full ownership of their home directory
    chown("/home/bishal", 1000, 1000);

    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    // 3. Set hostname
    sethostname("BishOS", 6);

    // 4. Set base environment variables
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("TERM", "xterm-256color", 1);

    // 5. Create default welcome note in /root and /home/bishal
    FILE *f_root = fopen("/root/welcome.txt", "w");
    if (f_root) {
        fprintf(f_root, "Welcome to BishOS (Root Mode)!\n\n"
                        "Useful commands to try:\n"
                        "  - ping -c 3 8.8.8.8\n"
                        "  - wget -qO- http://icanhazip.com\n"
                        "  - su - bishal (switch to normal user)\n"
                        "  - poweroff\n");
        fclose(f_root);
    }

    FILE *f_user = fopen("/home/bishal/welcome.txt", "w");
    if (f_user) {
        fprintf(f_user, "Welcome to BishOS, Bishal!\n\n"
                        "You are in your own home directory: /home/bishal\n"
                        "You have full read/write permissions here.\n"
                        "Try: touch myfile.txt && echo 'Hello BishOS' > myfile.txt\n");
        fclose(f_user);
        chown("/home/bishal/welcome.txt", 1000, 1000);
    }

    // 6. Load network driver and poll /sys/class/net until the kernel finishes PCIe registration
    system("insmod /lib/modules/e1000.ko 2>/dev/null || true");

    for (int i = 0; i < 30; i++) {
        if (access("/sys/class/net/eth0", F_OK) == 0) {
            break;
        }
        usleep(100000); // 100ms
    }

    // 7. Configure network interfaces & default gateway
    system("ifconfig lo 127.0.0.1 up");
    system("ifconfig eth0 10.0.2.15 netmask 255.255.255.0 broadcast 10.0.2.255 up 2>/dev/null || true");
    system("route add default gw 10.0.2.2 dev eth0 2>/dev/null || true");

    // 8. Welcome banner
    printf("\n");
    printf("==========================================\n");
    printf("         Welcome to BishOS v0.2!          \n");
    printf("     Linux Kernel + BusyBox + Network     \n");
    printf("==========================================\n");
    printf("\n");
    printf("Networking: Online (Intel e1000 + Google DNS: 8.8.8.8)\n");
    printf("User accounts: root, bishal (switch with: 'su - bishal')\n");
    printf("To cleanly shut down the OS, run: poweroff\n\n");

    // 9. PID 1 loop: launch login shell with controlling TTY
    while (1) {
        pid_t pid = fork();

        if (pid == 0) {
            // Create a new process session
            setsid();

            // Attach the kernel's console as the controlling terminal (TIOCSCTTY).
            // /dev/console follows the console= cmdline, so one image serves serial and VGA.
            int fd = open("/dev/console", O_RDWR);
            if (fd < 0) {
                fd = open("/dev/ttyS0", O_RDWR);
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
            // Parent (PID 1): reap EVERY child, since orphans are re-parented to us.
            // Only the death of our own shell breaks out to respawn it.
            int status;
            while (1) {
                pid_t dead = waitpid(-1, &status, 0);

                if (dead == pid) {
                    break; // our login shell exited: respawn it
                }

                if (dead < 0) {
                    if (errno == EINTR) {
                        continue; // interrupted by a signal: keep reaping
                    }
                    break; // ECHILD or unexpected error: nothing left to reap
                }

                // Any other pid was an orphan we just buried: keep waiting
            }
            printf("\n[BishOS] Shell session ended. Respawning shell...\n\n");
        } else {
            perror("[BishOS] fork failed");
            sleep(2);
        }
    }

    return 0;
}
