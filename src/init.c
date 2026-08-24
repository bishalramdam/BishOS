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

// Block devices we look for a persistent root filesystem on, in order.
// virtio is what QEMU gives us; sd*/nvme* cover VMware and real hardware.
static const char *root_devices[] = {
    "/dev/vda", "/dev/sda", "/dev/nvme0n1", NULL
};

#define NEWROOT "/newroot"

// Set by the shutdown signal handler; checked by the PID 1 reap loop.
static volatile sig_atomic_t shutdown_signal = 0;

static void handle_shutdown(int sig) {
    shutdown_signal = sig;
}

// Graceful shutdown: terminate everything, reap it, flush and remount the
// root read-only so ext4 is clean, then ask the kernel to power off / halt /
// reboot. Never returns.
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
    // Read-only remount is what keeps a persistent root from needing fsck
    // on the next boot. Harmless on a RAM-only initramfs boot.
    if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) != 0) {
        printf("[BishOS] note: could not remount / read-only (%s)\n", strerror(errno));
    }
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

// Early boot, initramfs stage: find a persistent root filesystem, move the
// pseudo-filesystems onto it, and hand over to the copy of ourselves living
// there. Returns only on failure -- on success we have exec'd and are gone.
//
// This is what an initramfs is actually for: not "being the OS", but finding
// the real root and getting out of the way.
static void try_switch_root(void) {
    const char *dev = NULL;

    // Block devices register a moment after PID 1 starts; wait briefly.
    for (int i = 0; i < 50 && !dev; i++) {
        for (int d = 0; root_devices[d]; d++) {
            if (access(root_devices[d], F_OK) == 0) {
                dev = root_devices[d];
                break;
            }
        }
        if (!dev) {
            usleep(100000); // 100ms
        }
    }

    if (!dev) {
        return; // no disk attached: caller falls back to running from RAM
    }

    mkdir(NEWROOT, 0755);
    if (mount(dev, NEWROOT, "ext4", 0, NULL) != 0) {
        printf("[BishOS] %s is not a mountable ext4 root (%s)\n", dev, strerror(errno));
        return;
    }

    // Refuse to switch into a filesystem with no init to hand over to.
    if (access(NEWROOT "/sbin/init", X_OK) != 0) {
        printf("[BishOS] %s has no /sbin/init, staying on initramfs\n", dev);
        umount(NEWROOT);
        return;
    }

    printf("[BishOS] persistent root found on %s, switching over...\n", dev);

    // Carry the already-mounted pseudo-filesystems across instead of
    // unmounting and re-mounting them on the other side.
    mount("/proc", NEWROOT "/proc", NULL, MS_MOVE, NULL);
    mount("/sys",  NEWROOT "/sys",  NULL, MS_MOVE, NULL);
    mount("/dev",  NEWROOT "/dev",  NULL, MS_MOVE, NULL);

    // Promote NEWROOT to /. After this the initramfs is unreachable (its
    // pages stay in RAM -- a real switch_root deletes them first, which we
    // skip for simplicity).
    if (chdir(NEWROOT) != 0 || mount(".", "/", NULL, MS_MOVE, NULL) != 0
            || chroot(".") != 0) {
        perror("[BishOS] switch_root failed");
        return;
    }
    chdir("/");

    // Hand over to the real root's init. The flag tells it that the early
    // boot work is already done, so it does not try to switch again.
    char *argv[] = {"/sbin/init", "--real-root", NULL};
    execv("/sbin/init", argv);
    perror("[BishOS] could not exec /sbin/init on the real root");
}

// Write a file only if it does not exist yet. On a persistent root the user's
// edits must survive reboots, so these are seeded once, not rewritten.
static void seed_file(const char *path, uid_t owner, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return; // already there (or unwritable) -- leave it alone
    }
    if (write(fd, text, strlen(text)) < 0) {
        perror("[BishOS] seed_file");
    }
    if (owner != 0 && fchown(fd, owner, owner) != 0) {
        perror("[BishOS] seed_file chown");
    }
    close(fd);
}

int main(int argc, char **argv) {
    // 1. Disable stdout buffering for immediate serial console output
    setvbuf(stdout, NULL, _IONBF, 0);

    int real_root = (argc > 1 && strcmp(argv[1], "--real-root") == 0);
    const char *storage;

    // 2. Mount essential kernel filesystems. After a switch_root these came
    // across with us, so only the initramfs stage needs to mount them.
    if (!real_root) {
        mkdir("/proc", 0755);
        mkdir("/sys", 0755);
        mkdir("/dev", 0755);
        mount("proc", "/proc", "proc", 0, NULL);
        mount("sysfs", "/sys", "sysfs", 0, NULL);
        mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

        // 3. Try to leave the initramfs for a real, persistent root.
        try_switch_root();

        // Still here: no usable disk. Keep going in RAM.
        printf("\n[BishOS] no persistent root; running from RAM "
               "(changes are lost on reboot)\n");
        storage = "RAM only (initramfs)";
    } else {
        storage = "persistent disk (ext4)";
        // /tmp belongs in RAM even when the root is on disk.
        mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
    }

    // 4. Home directory for the unprivileged user
    mkdir("/home", 0755);
    mkdir("/home/bishal", 0755);
    chown("/home/bishal", 1000, 1000);

    // 5. Set hostname
    sethostname("BishOS", 6);

    // 6. Set base environment variables
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("TERM", "xterm-256color", 1);

    // 7. Seed welcome notes (once -- see seed_file)
    seed_file("/root/welcome.txt", 0,
              "Welcome to BishOS (Root Mode)!\n\n"
              "Useful commands to try:\n"
              "  - ping -c 3 8.8.8.8\n"
              "  - wget -qO- http://icanhazip.com\n"
              "  - df -h /            (is this root persistent?)\n"
              "  - su - bishal (switch to normal user)\n"
              "  - poweroff\n");
    seed_file("/home/bishal/welcome.txt", 1000,
              "Welcome to BishOS, Bishal!\n\n"
              "You are in your own home directory: /home/bishal\n"
              "You have full read/write permissions here.\n"
              "Try: touch myfile.txt && echo 'Hello BishOS' > myfile.txt\n");

    // 8. Bring up networking. DHCP first: QEMU's SLIRP, VMware's NAT, and real
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

    // 9. Shutdown signals. BusyBox poweroff/halt/reboot (without -f) do not
    // call reboot(2) themselves -- they signal PID 1 and trust it to shut
    // down cleanly: SIGUSR2 = poweroff, SIGUSR1 = halt, SIGTERM = reboot.
    // No SA_RESTART: the signal must interrupt waitpid() with EINTR.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    // 10. Welcome banner
    printf("\n");
    printf("==========================================\n");
    printf("         Welcome to BishOS v0.4!          \n");
    printf("     Linux Kernel + BusyBox + Network     \n");
    printf("==========================================\n");
    printf("\n");
    printf("Storage:    %s\n", storage);
    printf("Networking: %s (DNS: 8.8.8.8)\n", net_mode);
    printf("User accounts: root, bishal (switch with: 'su - bishal')\n");
    printf("To cleanly shut down the OS, run: poweroff\n\n");

    // 11. PID 1 loop: launch login shell with controlling TTY
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
            char *sh_argv[] = {"/bin/sh", "-l", NULL};
            execv("/bin/sh", sh_argv);

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
