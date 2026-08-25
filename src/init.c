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
#include <time.h>

// Version comes from the Makefile (-DBISHOS_VERSION=0.3.0). The two-step
// stringify is what turns that bare token into a string literal.
#ifndef BISHOS_VERSION
#define BISHOS_VERSION dev
#endif
#define STRINGIFY(x) #x
#define VERSION_STRING(x) STRINGIFY(x)

#define NEWROOT "/newroot"

// Service table. Each line is:  <name> <respawn|once|console> <command...>
// Comments start with #, and the command is split on whitespace only -- there
// is no quoting, because a shell to do the quoting is exactly what an init
// must not have to depend on.
#define SERVICES_FILE "/etc/bishos/services"
#define MAX_SERVICES 32
#define MAX_ARGS 16

// Only ever adopt a filesystem we made. mke2fs labels the disk image BISHOS,
// and we check that label by reading the superblock directly -- never by
// mounting first. This matters on real hardware: without it, booting from a
// USB stick on a PC would happily mount and switch_root into whatever Linux
// install it found on the internal drive.
#define ROOT_LABEL "BISHOS"

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
// True only if the device holds an ext filesystem whose label is ours. The
// ext superblock sits at byte 1024: magic 0xEF53 at +56, label at +120.
// Reading it costs nothing and, unlike mounting, cannot modify the device.
static int is_bishos_root(const char *dev) {
    unsigned char sb[1024];
    int fd = open(dev, O_RDONLY);

    if (fd < 0) {
        return 0;
    }
    if (pread(fd, sb, sizeof sb, 1024) != (ssize_t) sizeof sb) {
        close(fd);
        return 0;
    }
    close(fd);

    if ((sb[56] | (sb[57] << 8)) != 0xEF53) {
        return 0; // not ext2/3/4 at all
    }

    char label[17];
    memcpy(label, sb + 120, 16);
    label[16] = '\0';
    return strcmp(label, ROOT_LABEL) == 0;
}

// Look for our labelled filesystem on every block device the kernel knows.
// /proc/partitions lists whole disks and partitions alike, so this finds a
// virtio disk, a USB stick's second partition, NVMe or an SD card without
// hardcoding any names. Checking the label makes scanning everything safe.
static int find_bishos_root(char *out, size_t outsz) {
    char line[256];
    FILE *f = fopen("/proc/partitions", "r");

    if (!f) {
        return 0;
    }
    // two header lines before the entries
    if (fgets(line, sizeof line, f) && fgets(line, sizeof line, f)) {
        while (fgets(line, sizeof line, f)) {
            unsigned major, minor;
            unsigned long long blocks;
            char name[64], dev[80];

            if (sscanf(line, "%u %u %llu %63s", &major, &minor, &blocks, name) != 4) {
                continue;
            }
            snprintf(dev, sizeof dev, "/dev/%s", name);
            if (is_bishos_root(dev)) {
                snprintf(out, outsz, "%s", dev);
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

static void try_switch_root(void) {
    char devbuf[80];
    const char *dev = NULL;

    // Block devices register a moment after PID 1 starts; wait briefly. USB
    // storage is the slow case -- enumeration can take a couple of seconds.
    for (int i = 0; i < 50 && !dev; i++) {
        if (find_bishos_root(devbuf, sizeof devbuf)) {
            dev = devbuf;
            break;
        }
        usleep(100000); // 100ms
    }

    if (!dev) {
        return; // nothing of ours anywhere: caller falls back to running from RAM
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


enum action {
    ACT_RESPAWN,  // restart whenever it exits
    ACT_ONCE,     // run at boot, do not restart
    ACT_CONSOLE   // respawn, and give it the controlling terminal
};

struct service {
    char name[32];
    enum action action;
    char cmdline[224];       // storage the argv pointers point into
    char *argv[MAX_ARGS];
    pid_t pid;               // 0 when not running
    time_t started;
    int failures;
    time_t retry_after;
    int finished;            // an ACT_ONCE that has already run
};

static struct service services[MAX_SERVICES];
static int service_count;

// Exists only so SIGCHLD interrupts the sleep in the supervise loop: a dead
// child is then noticed at once instead of up to a second later.
static void handle_sigchld(int sig) {
    (void) sig;
}

static void tokenize(struct service *s) {
    char *p = s->cmdline;
    int n = 0;

    while (*p && n < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (!*p) {
            break;
        }
        s->argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    s->argv[n] = NULL;
}

static void add_service(const char *name, enum action action, const char *cmd) {
    struct service *s;

    if (service_count >= MAX_SERVICES) {
        printf("[BishOS] too many services, ignoring %s\n", name);
        return;
    }
    s = &services[service_count];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    s->action = action;
    snprintf(s->cmdline, sizeof s->cmdline, "%s", cmd);
    tokenize(s);
    if (s->argv[0]) {
        service_count++;
    }
}

static void load_services(void) {
    char line[320];
    FILE *f = fopen(SERVICES_FILE, "r");

    if (!f) {
        // No table: behave exactly as BishOS did before services existed --
        // a login shell on the console, respawned forever.
        add_service("console", ACT_CONSOLE, "/bin/sh -l");
        return;
    }

    while (fgets(line, sizeof line, f)) {
        char name[32], action[16];
        char *p = line;
        int off = 0;
        enum action act;

        line[strcspn(line, "\n")] = '\0';
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\0') {
            continue;
        }
        if (sscanf(p, "%31s %15s %n", name, action, &off) < 2 || off == 0) {
            printf("[BishOS] ignoring malformed service line: %s\n", p);
            continue;
        }

        if (strcmp(action, "respawn") == 0) {
            act = ACT_RESPAWN;
        } else if (strcmp(action, "once") == 0) {
            act = ACT_ONCE;
        } else if (strcmp(action, "console") == 0) {
            act = ACT_CONSOLE;
        } else {
            printf("[BishOS] service %s: unknown action '%s'\n", name, action);
            continue;
        }
        add_service(name, act, p + off);
    }
    fclose(f);

    if (service_count == 0) {
        add_service("console", ACT_CONSOLE, "/bin/sh -l");
    }
}

static void start_service(struct service *s) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("[BishOS] fork");
        s->retry_after = time(NULL) + 2;
        return;
    }

    if (pid == 0) {
        // Every service gets its own session, so a Ctrl-C aimed at the shell
        // cannot reach a daemon. Only the console service claims the
        // terminal, which is what gives it job control.
        setsid();

        if (s->action == ACT_CONSOLE) {
            int fd = open("/dev/console", O_RDWR);

            if (fd < 0) {
                fd = open("/dev/ttyS0", O_RDWR);
            }
            if (fd >= 0) {
                ioctl(fd, TIOCSCTTY, 1);
                dup2(fd, 0);
                dup2(fd, 1);
                dup2(fd, 2);
                if (fd > 2) {
                    close(fd);
                }
            }
        }

        execv(s->argv[0], s->argv);

        // Only reached if exec failed. For the console, fall back to busybox,
        // which is static and always present, before giving up.
        if (s->action == ACT_CONSOLE) {
            char *bb[] = {"/bin/busybox", "sh", "-l", NULL};
            execv("/bin/busybox", bb);
        }
        fprintf(stderr, "[BishOS] %s: cannot execute %s: %s\n",
                s->name, s->argv[0], strerror(errno));
        _exit(127);
    }

    s->pid = pid;
    s->started = time(NULL);
}

// Start anything that should be running and is not.
static void start_due_services(void) {
    time_t now = time(NULL);

    for (int i = 0; i < service_count; i++) {
        struct service *s = &services[i];

        if (s->pid == 0 && !s->finished && now >= s->retry_after) {
            start_service(s);
        }
    }
}

// A child died. If it is one of ours, decide whether to start it again.
static void service_exited(pid_t pid, int status) {
    time_t now = time(NULL);

    for (int i = 0; i < service_count; i++) {
        struct service *s = &services[i];

        if (s->pid != pid) {
            continue;
        }
        s->pid = 0;

        if (s->action == ACT_ONCE) {
            s->finished = 1;
            return;
        }

        // Back off when a service dies almost immediately, or a broken
        // command would be re-exec'd thousands of times a second.
        if (now - s->started < 2) {
            int delay;

            s->failures++;
            delay = s->failures * 2;
            if (delay > 30) {
                delay = 30;
            }
            s->retry_after = now + delay;
            printf("[BishOS] %s died immediately (status %d), retrying in %ds\n",
                   s->name, WEXITSTATUS(status), delay);
        } else {
            s->failures = 0;
            s->retry_after = 0;
            if (s->action == ACT_CONSOLE) {
                printf("\n[BishOS] Shell session ended. Respawning shell...\n\n");
            } else {
                printf("[BishOS] %s exited, restarting\n", s->name);
            }
        }
        return;
    }
    // Not one of ours: an orphan the kernel re-parented to us. Reaping it was
    // the entire job.
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

    // 3a. Pseudo-terminals. Mounted here, after the switch_root decision, so
    // it happens exactly once on either path. Without devpts nothing can
    // allocate a pty, which silently breaks ssh, tmux, screen and script.
    // mode=0620,gid=5 is the usual owner/permission pair for a pty (group
    // "tty"); ptmxmode makes /dev/pts/ptmx usable by non-root.
    mkdir("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", 0,
              "mode=0620,gid=5,ptmxmode=0666") != 0) {
        printf("[BishOS] warning: no /dev/pts (%s) -- ssh and tmux will not work\n",
               strerror(errno));
    }

    // POSIX shared memory. Python's multiprocessing and many servers expect
    // it, and its absence surfaces as a confusing unrelated error.
    mkdir("/dev/shm", 01777);
    mount("tmpfs", "/dev/shm", "tmpfs", 0, "mode=1777");

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

    // SIGCHLD is handled only so that it interrupts sleep() below.
    sa.sa_handler = handle_sigchld;
    sigaction(SIGCHLD, &sa, NULL);

    // 10. Welcome banner
    printf("\n");
    printf("==========================================\n");
    printf("        Welcome to BishOS v%s\n", VERSION_STRING(BISHOS_VERSION));
    printf("     Linux Kernel + BusyBox + Network     \n");
    printf("==========================================\n");
    printf("\n");
    printf("Storage:    %s\n", storage);
    printf("Networking: %s (DNS: 8.8.8.8)\n", net_mode);
    printf("User accounts: root, bishal (switch with: 'su - bishal')\n");
    printf("To cleanly shut down the OS, run: poweroff\n\n");

    // 11. Supervise. Load the service table, start everything in it, then
    // reap forever: a dead child is either a service to restart or an orphan
    // the kernel re-parented to us, and one loop handles both.
    load_services();
    printf("Services:   %d configured\n\n", service_count);

    while (1) {
        int status;
        pid_t dead;

        if (shutdown_signal) {
            do_shutdown(shutdown_signal);
        }

        start_due_services();

        dead = waitpid(-1, &status, WNOHANG);
        if (dead > 0) {
            service_exited(dead, status);
            continue;   // there may be more to reap before sleeping
        }
        if (dead < 0 && errno != ECHILD && errno != EINTR) {
            perror("[BishOS] waitpid");
        }

        // Wait for something to happen. SIGCHLD cuts this short, so a dead
        // child is handled at once; the timeout is what lets a backed-off
        // restart eventually come due.
        sleep(1);
    }

    return 0;
}
