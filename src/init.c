#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>

// Set by the shutdown signal handler; checked by the PID 1 reap loop.
static volatile sig_atomic_t shutdown_signal = 0;

static void handle_shutdown(int sig) {
    shutdown_signal = sig;
}

// Graceful shutdown: terminate everything, reap it, sync, then ask the
// kernel to power off / halt / reboot. Never returns.
static void do_shutdown(int sig) {
    const char *what = (sig == SIGTERM) ? "Rebooting"
                     : (sig == SIGUSR1) ? "Halting"
                                        : "Powering off";
    printf("\n[BishOS] %s: terminating all processes...\n", what);

    kill(-1, SIGTERM);   // from PID 1 this signals everyone except us
    sleep(2);            // grace period for clean exits
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;                // bury whatever is left

    sync();
    printf("[BishOS] Bye!\n");

    if (sig == SIGTERM) {
        reboot(RB_AUTOBOOT);
    } else if (sig == SIGUSR1) {
        reboot(RB_HALT_SYSTEM);
    } else {
        reboot(RB_POWER_OFF);
    }
    perror("[BishOS] reboot() failed");  // reboot(2) only returns on error
    while (1) {
        sleep(60);       // PID 1 must never exit, even here
    }
}

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

    // 6. Bring up networking. DHCP first: QEMU's SLIRP, VMware's NAT, and real
    // routers all run DHCP servers, so one code path serves every host. Only
    // if nothing answers (-n: give up, -t/-T: 3 tries x 2s) fall back to
    // QEMU SLIRP's fixed layout so direct-kernel boots still work offline.
    const char *net_mode;
    system("ifconfig lo 127.0.0.1 up");
    system("ifconfig eth0 up 2>/dev/null");
    if (system("udhcpc -i eth0 -s /usr/share/udhcpc/default.script "
               "-n -q -t 3 -T 2 >/dev/null 2>&1") == 0) {
        net_mode = "DHCP";
    } else {
        system("ifconfig eth0 10.0.2.15 netmask 255.255.255.0 broadcast 10.0.2.255 up 2>/dev/null || true");
        system("route add default gw 10.0.2.2 dev eth0 2>/dev/null || true");
        net_mode = "static fallback (10.0.2.15)";
    }

    // 7. Shutdown signals. BusyBox poweroff/halt/reboot (without -f) do not
    // call reboot(2) themselves -- they signal PID 1 and trust it to shut
    // down cleanly: SIGUSR2 = poweroff, SIGUSR1 = halt, SIGTERM = reboot.
    // No SA_RESTART: the signal must interrupt waitpid() with EINTR.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    // 8. Welcome banner
    printf("\n");
    printf("==========================================\n");
    printf("         Welcome to BishOS v0.3!          \n");
    printf("     Linux Kernel + BusyBox + Network     \n");
    printf("==========================================\n");
    printf("\n");
    printf("Networking: %s (DNS: 8.8.8.8)\n", net_mode);
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
                if (shutdown_signal) {
                    do_shutdown(shutdown_signal);
                }

                pid_t dead = waitpid(-1, &status, 0);

                if (dead == pid) {
                    break; // our login shell exited: respawn it
                }

                if (dead < 0) {
                    if (errno == EINTR) {
                        continue; // signal arrived: loop re-checks shutdown_signal
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
