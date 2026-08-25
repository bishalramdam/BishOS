# BishOS 🐧

[![Build ISOs](https://github.com/bishalramdam/BishOS/actions/workflows/build-iso.yml/badge.svg)](https://github.com/bishalramdam/BishOS/actions/workflows/build-iso.yml)

A minimal Linux operating system built modularly like **Lego blocks**: combining the mainline **Linux kernel** (compiled from source) with custom and open-source userspace components (`/init`, shell, standard utilities) to learn operating system fundamentals from the ground up.

Builds for **two architectures** from the same source:

| | x86_64 (default) | arm64 |
| --- | --- | --- |
| Kernel image | `bzImage` | `Image` |
| ISO firmware | UEFI **and** legacy BIOS | UEFI |
| QEMU machine | PC (`qemu-system-x86_64`) | `virt` (`qemu-system-aarch64`) |
| Acceleration on Apple Silicon | none (TCG software emulation) | **HVF hardware virtualization** |
| Serial console | `ttyS0` (16550 UART) | `ttyAMA0` (PL011 UART) |
| Network card | Intel e1000 | virtio-net |

---

## 🧱 The Lego Block Architecture

```
+-------------------------------------------------------------+
| 1. The Linux Kernel (x86_64 bzImage / arm64 Image)          |
|    - Manages hardware, CPU scheduling, memory, and devices  |
+-------------------------------------------------------------+
                              │
                     boots and executes
                              ▼
+-------------------------------------------------------------+
| 2. The Initramfs (Initial RAM Filesystem / rootfs)          |
|    - SVR4 newc cpio.gz archive unpacked into memory         |
|    - Contains root directory structure (/bin, /proc, /sys)  |
+-------------------------------------------------------------+
                              │
                  launches PID 1 (first process)
                              ▼
+-------------------------------------------------------------+
| 3. Early Init (initramfs stage, PID 1)                      |
|    - Mounts /proc, /sys, /dev                               |
|    - Finds a persistent ext4 root on vda/sda/nvme0n1        |
|    - switch_root into it and hands over to /sbin/init       |
|    - No disk? Keeps running from RAM instead                |
+-------------------------------------------------------------+
                              │
                    switch_root (MS_MOVE)
                              ▼
+-------------------------------------------------------------+
| 4. Real Root Filesystem (persistent ext4 disk)              |
|    - Minimal static C init (PID 1) + BusyBox userland       |
|    - DHCP networking, login shell, graceful shutdown        |
|    - Files written here survive reboots                     |
+-------------------------------------------------------------+
```

---

## 🚀 Quickstart

### Prerequisites

| Tool | Purpose | Install (macOS) |
| --- | --- | --- |
| **Docker Desktop** | Linux build environment (ELF static binaries) | [Docker Desktop](https://www.docker.com/) |
| **QEMU** | Hardware emulator to boot the OS | `brew install qemu` |
| **Make** | Build automation | Built-in |

### Build & Run

1. **Build everything** — compiles Linux 6.18.46 from source (checksum-pinned),
   compiles `init`, and packs the initramfs:
   ```bash
   make all              # x86_64
   make ARCH=arm64 all   # arm64 (native + HVF-accelerated on Apple Silicon)
   ```
   The kernel source lives in a Docker volume; the first build compiles from
   scratch (~10-25 min), later builds are incremental.

2. **Boot in QEMU** (creates a 1 GB persistent ext4 disk on first run):
   ```bash
   make run              # x86_64 (software-emulated on Apple Silicon)
   make ARCH=arm64 run   # arm64 (near-native speed on Apple Silicon)
   ```

3. **First boot** sets the machine up. It asks for a username (default
   `bishal`), a hostname (default `BishOS`), and then a password for `root`
   and for your new account. Nothing is baked into the image -- an ISO
   carrying its author's username and machine name would put a stranger's
   details on your computer. Then log in, and use `sudo` when you need root:
   ```bash
   sudo apk add python3     # install something
   sudo su                  # become root
   ```
   Only a persistent root has accounts. A RAM-only boot (the ISO with no
   BishOS disk attached) drops straight into a root shell instead, because
   nothing written to `/etc` there would survive to be asked for again.

4. **SSH in**, once you have asked for it. BishOS does not listen on port 22
   until you say so. In the guest:
   ```bash
   sudo touch /etc/bishos/ssh.enabled
   ```
   Then from another terminal on the host:
   ```bash
   ssh -p 2222 you@localhost
   ```
   `make run` forwards that port into the guest. The answer is remembered
   across reboots, and `sudo rm /etc/bishos/ssh.enabled && sudo pkill sshd`
   takes it back. Root is refused over SSH by design -- log in as your own
   account and use `sudo`.

5. **Exit QEMU**:
   Press `Ctrl + A`, release, then press `X`.

   The disk at `build/$ARCH/bishos-disk.img` persists across boots and is
   never overwritten by a rebuild. It defaults to 5 GB and is sparse, so it
   costs host disk only as it fills.
   ```bash
   make ARCH=arm64 disk-update               # refresh init and /etc, keep your data
   make ARCH=arm64 disk-grow                 # bigger, keeping your data
   make ARCH=arm64 DISK_SIZE=20G disk-grow   # any size you like
   make ARCH=arm64 disk-reset                # wipe and start clean
   ```
   Growing works in place because the image is a whole-disk ext4 filesystem
   with no partition table -- there is no partition to move before resizing.

   `disk-update` is the one to remember. Because a rebuild never touches an
   existing image, a disk in daily use goes on booting whatever `init` it was
   created with, and new features simply do not appear -- which looks exactly
   like new features being broken. It replaces `/sbin/init` and the BishOS
   configuration under `/etc`, and leaves accounts, home directories and
   installed packages alone.

### Persistent live USB

init looks for its root on *any* block device -- whole disks and partitions
alike -- and adopts the first ext4 filesystem labelled `BISHOS`. That is the
whole mechanism, so a USB stick gains persistence by giving it such a
partition.

Do not `dd` the ISO and try to add a partition afterwards. That is documented
widely and does not work here: `grub-mkrescue` leaves a GPT whose secondary
table overlaps the last partition, so `sgdisk -e` aborts with
`Aborting write of new partition table` and the disk keeps reporting itself as
27 MB. `sgdisk -n 0:0:0` then makes a 1.5 KiB partition in a gap rather than a
28 GB one at the end.

Install to it properly instead. On Linux, with `/dev/sdX` the stick -- check
with `lsblk -o NAME,SIZE,MODEL,TRAN` first, because getting this wrong
destroys the machine you are typing on:

```bash
sudo wipefs -a /dev/sdX
sudo sgdisk -Z /dev/sdX
sudo sgdisk -n 1:0:+512M -t 1:ef00 -c 1:BISHOSEFI \
            -n 2:0:0     -t 2:8300 -c 2:BISHOS /dev/sdX
sudo partprobe /dev/sdX
sudo mkfs.vfat -F32 -n BISHOSEFI /dev/sdX1
sudo mkfs.ext4 -L BISHOS /dev/sdX2
```

No bootloader needs installing: the ISO already contains one, and it is the
same binary that draws the menu when the ISO is booted directly. Copy the
whole thing onto the EFI partition -- 27 MB into 512 MB, and FAT is
case-insensitive, so the ISO's `/efi/boot/bootx64.efi` lands exactly where
firmware looks for `/EFI/BOOT/BOOTX64.EFI` on a removable device:

```bash
sudo mkdir -p /mnt/iso /mnt/esp
sudo mount -o loop,ro bishos-x86_64_v0.10.1.iso /mnt/iso
sudo mount /dev/sdX1 /mnt/esp
sudo cp -r /mnt/iso/. /mnt/esp/
sudo umount /mnt/iso /mnt/esp && sync
```

The label must be exactly `BISHOS` -- it is compared with `strcmp`. Anything
else is ignored, which is what stops BishOS from adopting the internal drive
of whatever machine it is booted on. Check it with
`sudo blkid -s LABEL -o value /dev/sdX2 | cat -A`; a trailing space will show
up as `BISHOS $` and will not match.

Secure Boot must be off, since this GRUB is unsigned.

USB storage can take a surprisingly long time to appear. One stick failed its
first descriptor read at SuperSpeed, retried, and did not present a block
device until 23 seconds into the boot -- so init waits 45 seconds for a root
before falling back to RAM, printing its progress as it goes.
`bishos.rootwait=N` on the kernel command line changes that.

   The VM gets 2 GB of RAM by default (`make MEMORY=4G run` to change it).
   That figure also sizes `/tmp`: it is a tmpfs, so it defaults to half of
   RAM and lives *in* RAM. If an installer dies with `No space left on
   device` while `df -h /` shows plenty free, `/tmp` is what filled up --
   either raise `MEMORY` or point the installer at disk with
   `TMPDIR=/var/tmp`.

4. **Build a bootable ISO** -- GRUB on both architectures, same layout and
   the same `grub.cfg`; only the kernel binary differs:
   ```bash
   make ARCH=arm64 iso   # -> output/arm64/bishos-arm64_v0.10.1.iso   (UEFI)
   make iso              # -> output/x86_64/bishos-x86_64_v0.10.1.iso (UEFI + BIOS)
   ```
   `VERSION` in the Makefile is the single source of truth: it names the ISO
   and is compiled into init's banner, so a booted system always reports the
   version of the image it came from.

5. **Run things at boot** -- init reads `/etc/bishos/services`:
   ```
   <name>  <action>  <command...>

   respawn   start at boot, restart whenever it exits
   once      run at boot, do not restart
   console   respawn, and give it the controlling terminal
   ```
   A service that dies immediately is retried with a growing delay rather
   than in a tight loop. With no table present, init falls back to a login
   shell on the console, which is what it always did.

   Anything a service prints is collected by init and written to
   `/var/log/messages` under that service's own name, alongside the kernel's
   own log, and it survives reboots:
   ```bash
   sudo tail -20 /var/log/messages
   ```

6. **Install software** -- the persistent root ships with `apk`:
   ```bash
   sudo apk add python3     # or gcc, git, vim, curl, tmux, ...
   python3 --version
   ```
   `sudo`, because you are logged in as an ordinary user and installing
   software is root's business. A RAM-only boot has no accounts and no `sudo`, and the
   console there is root already.
   Installed packages survive reboots, because the root is a real disk.
   Prebuilt ISOs for both architectures are attached to every
   [CI run](https://github.com/bishalramdam/BishOS/actions) and to
   [tagged releases](https://github.com/bishalramdam/BishOS/releases), and
   published to GitHub Packages as OCI artifacts:
   ```bash
   oras pull ghcr.io/bishalramdam/bishos:0.10.1-arm64
   oras pull ghcr.io/bishalramdam/bishos:0.10.1-x86_64
   ```
   In VMware Fusion: New VM -> drag the ISO in -> "Other Linux 6.x 64-bit Arm".
   The same shell lands on the serial port in QEMU and on the screen in
   VMware -- the kernel gives /dev/console to the last console= that exists.

---

## 🗺️ Roadmap & Progress

- [x] **Phase 1: Proof of Concept (The 1-Program OS)**
  - [x] Custom static C `/init` running as PID 1
  - [x] `newc` cpio.gz initramfs generation
  - [x] Direct kernel boot in QEMU with serial console

- [x] **Phase 2: Interactive Shell & Core Utilities**
  - [x] Integrate static BusyBox
  - [x] Mount pseudofilesystems (`/proc`, `/sys`, `/dev`)
  - [x] Interactive `sh` prompt with controlling TTY & job control
  - [x] User accounts & shell profile (`/etc/passwd`, `/etc/group`, `/etc/profile`, `/etc/hostname`)

- [x] **Phase 3: Kernel Compilation from Source**
  - [x] Linux 6.18.46 (LTS) from kernel.org, pinned by SHA-256
  - [x] Cross-compiled for x86_64 / built natively for arm64 in Docker
  - [ ] Custom minimal `defconfig` (currently stock `defconfig` per arch)

- [x] **Phase 4: Networking & Internet Connectivity**
  - [x] NIC driver built into the kernel (e1000 on x86_64, virtio-net on arm64)
  - [x] Auto-configuration of `eth0` and default gateway
  - [x] Google DNS resolution (`8.8.8.8`, `8.8.4.4`)
  - [x] Live Internet connectivity, ICMP ping, and HTTP web fetching

- [x] **Phase 5: Multi-Architecture Support**
  - [x] Single Makefile drives both arches (`make ARCH=arm64 ...`)
  - [x] Shared kernel source tree, per-arch out-of-tree (`O=`) build dirs
  - [x] Hardware-accelerated arm64 boot on Apple Silicon via HVF

- [x] **Phase 6: Real Hypervisors & Init Lifecycle**
  - [x] UEFI-bootable ISO via GRUB (`make ARCH=arm64 iso`), verified on
        QEMU + EDK2 firmware and VMware Fusion
  - [x] DHCP auto-configuration (udhcpc) with static QEMU fallback
  - [x] Graceful shutdown: `poweroff`/`reboot`/`halt` signal PID 1
        (SIGUSR2/SIGTERM/SIGUSR1), which terminates and reaps all
        processes, syncs, and calls `reboot(2)`
  - [x] One GRUB config for every architecture; x86_64 ISOs carry both a
        UEFI and a legacy-BIOS boot path
  - [x] CI builds and boot-tests both ISOs on native runners, publishes
        them to GitHub Packages (ghcr.io) and to tagged releases
  - [x] Semantic versioning: one `VERSION` names the ISO and the banner

---

- [x] **Phase 7: Persistent Root Filesystem**
  - [x] Raw ext4 disk image built with `mke2fs -d` (no loop device, no
        privileged container)
  - [x] `switch_root` out of the initramfs into the real root, carrying
        `/proc`, `/sys`, `/dev` across with `MS_MOVE`
  - [x] Graceful fallback to running from RAM when no disk is attached
        (so the ISO and direct-kernel boots still work)
  - [x] The root is found by scanning every block device for the `BISHOS`
        label, so it can live on a disk, a USB partition or an SD card --
        and a foreign disk is never adopted
  - [x] Read-only remount on shutdown so ext4 stays clean
  - [x] `make disk-update` refreshes `init` and the BishOS configuration on
        an existing disk without touching accounts, home directories or
        installed packages. A rebuild never overwrites a disk that already
        exists -- which protects your data, and quietly leaves a machine in
        daily use booting whatever `init` it was created with

- [x] **Phase 8: Package Manager**
  - [x] Alpine's `apk` (static binary) on the persistent root, with Alpine's
        signing keys so downloads are verified, not blindly trusted
  - [x] Repositories pinned to a specific Alpine release
  - [x] CA certificate bundle shipped, so HTTPS can actually be verified
  - [x] Installs dynamically-linked software: the first package pulls in
        `musl`, which provides the loader the whole repository needs
  - [x] Clock kept correct by `ntpd`, which matters because TLS validates
        certificate *dates* -- a wrong clock fails looking like a CA problem

- [x] **Phase 9: Accounts**
  - [x] `/etc/shadow` ships *locked* -- `!`, not a hash -- so no default
        password is ever committed, and nothing can log in until one is set
  - [x] First boot on a persistent root asks for a username and a hostname,
        creates the account with `adduser`, puts it in `wheel`, and asks for
        both passwords once, writing SHA-512 crypt hashes to `/etc/shadow`.
        No account and no machine name ship in the image: an OS that arrives
        already called BishOS, with its author's username on it, is somebody
        else's machine
  - [x] The console runs `login`, not a root shell, so a session starts by
        saying who you are
  - [x] `sudo` on the disk, with `wheel` allowed in a `/etc/sudoers.d`
        drop-in. It lives on the root and not in the initramfs because
        busybox has no `sudo` applet and the real one is dynamically linked
  - [x] A RAM-only boot deliberately keeps the old root shell: `/etc` there
        is rebuilt from the initramfs every boot, so a password set on the
        ISO could never be asked for again

- [x] **Phase 10: Remote Access**
  - [x] `sshd` supervised from the service table, on a machine that already
        had the two things it needs: ptys, and passwords to check
  - [x] **Off until asked for.** A service that listens because it happens to
        be installed is a service nobody decided to run. Port 22 opens only
        while `/etc/bishos/ssh.enabled` exists, the answer survives reboots,
        and it takes effect in seconds without one
  - [x] Host keys generated on the machine the first time ssh is actually
        turned on -- never shipped in the image, and never generated at all
        for a machine that never wants them. A host key in an ISO is the same
        private key on every install, which is what host keys exist to prevent
  - [x] `PermitRootLogin no`, via a `/etc/ssh/sshd_config.d` drop-in that
        survives reinstalling the package. `root` is the account name every
        scanner tries first, and your own account plus `sudo` is a better
        road in
  - [x] `make run` forwards host port 2222 to the guest, because QEMU's
        user-mode networking otherwise gives the guest no inbound route:
        `ssh -p 2222 you@localhost`

- [x] **Phase 11: Logging & the Console**
  - [x] Kernel log persisted to `/var/log/messages` by `syslogd` and `klogd`,
        capped by rotation so a log cannot fill the root filesystem
  - [x] Every non-console service gets its stdout and stderr on a pipe that
        init forwards to syslogd under that service's own name. Most daemons
        never call `syslog(3)` -- they just print -- so without this their
        output scrolled off the console and was gone
  - [x] The console session runs on the real tty the kernel names in
        `/sys/class/tty/console/active`, not on `/dev/console`. The latter is
        a redirector, not the session's controlling terminal, so `tcsetpgrp`
        against it fails -- which a shell only discovers on the way out, and
        every shutdown ended with `can't set tty process group`
  - [x] The console session is sent `SIGHUP` before the general `SIGTERM` at
        shutdown: an interactive shell ignores `SIGTERM` by design, and
        `SIGHUP` is what actually means "your terminal is going away"

- [x] **Phase 12: A Screen That Stays On**
  - [x] Network settings read from `/etc/bishos/network` and applied by
        `/etc/bishos/net-up`, rather than being decided in `init.c` where
        changing an address meant a recompile, a rebuilt initramfs and a
        reboot. `sudo /etc/bishos/net-up` re-applies them on the spot
  - [x] The old compiled-in path is still in `init`, used when the script is
        missing, unrunnable or fails -- a typo in a config file should not
        cost you the network you would need to fetch the fix
  - [x] DNS comes from the DHCP server. The lease used to be discarded and
        Google's resolvers written on every renewal, so a router's own DNS,
        and every local name only it could resolve, simply did not exist
  - [x] `DRM_FBDEV_EMULATION`, `DRM_SIMPLEDRM` and `SYSFB_SIMPLEFB`. x86_64's
        defconfig builds `DRM_I915` in, and a DRM driver evicts the firmware
        framebuffer when it probes -- without these it then provides no
        console, so on any machine with Intel graphics the screen lights up,
        the driver loads, and it goes black
  - [x] Invisible to every test: QEMU has no Intel GPU, so `i915` never
        probes, `efifb` is never evicted, and CI passed for four releases
        while the ISO was unusable on the hardware it was built for
  - [x] Boot menu entries for diagnosing it on a machine you cannot iterate
        on: `nomodeset` to rule graphics in or out, and `quiet` dropped so the
        boot narrates itself

## 📂 Project Layout

```
├── .github/workflows/
│   └── build-iso.yml   # CI: builds + boot-tests both ISOs, publishes releases
├── Makefile            # Build and run automation (ARCH=x86_64 | arm64)
├── Dockerfile.kernel   # Kernel build container (native gcc + x86_64 cross-toolchain)
├── Dockerfile.iso      # ISO packaging container (GRUB + xorriso, per-arch)
├── grub/
│   └── grub.cfg        # Boot menu -- one config, every architecture
├── etc/                # System configuration overlay, copied into the rootfs
│   ├── bishos/         # Everything init reads or runs
│   │   ├── services    # The service table: what runs at boot, and how
│   │   ├── console     # Console session: first-boot passwords, then login
│   │   ├── net-up      # Applies the network settings below
│   │   ├── network     # Interface, mode, addresses, DNS policy
│   │   ├── ntp-step    # Writes the corrected clock back to the RTC
│   │   └── sshd        # Waits for ssh.enabled, makes host keys, runs sshd
│   ├── passwd          # root and sshd only -- your account is made on first boot
│   ├── shadow          # Ships LOCKED -- "!", never a hash. Set on first boot
│   ├── group           # Group definitions, including wheel
│   ├── sudoers.d/
│   │   └── wheel       # Members of wheel may run anything, with a password
│   ├── ssh/sshd_config.d/
│   │   └── bishos.conf # PermitRootLogin no, as a drop-in the package cannot undo
│   ├── hostname        # Machine name; chosen on first boot, BishOS by default
│   ├── hosts           # Local loopback resolution
│   ├── profile         # Login shell environment, aliases, colors
│   ├── resolv.conf     # Fallback nameservers; DHCP overwrites this per lease
│   └── udhcpc/
│       └── default.script # DHCP event handler: address, route, nameservers
├── src/
│   └── init.c          # PID 1: pseudo-filesystems, switch_root, service
│                       # supervision, log collection, shutdown
├── build/              # Scratch, gitignored -- make clean deletes all of it
│   ├── x86_64/
│   │   ├── bzImage     # Compiled Linux kernel (x86_64)
│   │   ├── rootfs/     # Root filesystem staging area
│   │   └── initramfs.cpio.gz
│   └── arm64/
│       ├── Image       # Compiled Linux kernel (arm64)
│       ├── rootfs/
│       ├── initramfs.cpio.gz
│       └── bishos-disk.img  # Persistent ext4 root (survives rebuilds)
├── output/             # Finished ISOs, gitignored. make clean leaves these
│   ├── x86_64/
│   │   └── bishos-x86_64_v0.10.1.iso
│   └── arm64/
│       └── bishos-arm64_v0.10.1.iso
├── TODO.md             # What is done, what is declined and why, what is left
└── README.md
```


---

## 📝 Notes

### Why `apk` and not `apt`

BishOS builds its userland against **musl** (Alpine's toolchain), while every
Debian `.deb` -- including `apt` and `dpkg` themselves -- is compiled against
**glibc** and expects perl, a `/var/lib/dpkg` database, and Debian's
filesystem layout. Running real `apt` therefore means replacing the whole
userland with Debian's, which would make BishOS "Debian with a custom kernel
and init" rather than a system built from parts.

The natural fit is Alpine's **`apk`**, which is what BishOS ships: a
self-contained static binary that speaks to the same Alpine mirrors the build
already uses for BusyBox. Its packages are built for exactly this userland.

### How installing dynamic software works

Every binary BishOS builds itself is **static** -- it carries its own copy of
the library code, which is why it runs on a system with no libraries at all.
Alpine's packages are **dynamic**: they expect to find a loader at
`/lib/ld-musl-<arch>.so.1` when they start, and on a freshly built BishOS
that file does not exist.

The way out is that `musl` is itself a package. The first `apk add` installs
it, the loader appears, and from then on the entire Alpine repository is
installable. So `apk add python3` is not a special case -- it is apk pulling
`musl`, then Python's dependencies, then Python.

One thing a from-scratch system does not get for free: a **CA certificate
bundle**. Without `/etc/ssl/certs/ca-certificates.crt` there is nothing on
disk to check a server's certificate against, and `apk` refuses every
download with `TLS: server certificate not trusted`. BishOS ships the bundle
(175 KB) in the rootfs.

Note that BusyBox's own `wget` never uses it. BusyBox delegates TLS to its
`ssl_client` applet, whose minimal TLS implementation encrypts but has no
certificate verification compiled in -- hence `TLS certificate validation
not implemented`. It is a note, not an error: the transfer works, it is just
unauthenticated. For HTTPS that actually verifies the peer, install a real
client once apk is up (`apk add curl`); it, and anything else from the
repository such as Python's `ssl` module, will use the shipped bundle.

`apk` lives on the persistent root and deliberately **not** in the initramfs:
the initramfs is loaded into RAM in full on every boot, so every megabyte is
a permanent cost, and a package manager whose installs vanish at the next
reboot would be pointless.

### Changing the network settings

They live in `/etc/bishos/network`, which is shell syntax because
`/etc/bishos/net-up` sources it:

```sh
IFACE=eth0
MODE=dhcp                    # or: static
FALLBACK_ADDRESS=10.0.2.15   # used by static, and when DHCP finds nobody
FALLBACK_NETMASK=255.255.255.0
FALLBACK_GATEWAY=10.0.2.2
FALLBACK_DNS="8.8.8.8 8.8.4.4"
USE_DHCP_DNS=yes             # no = ignore the DHCP server's nameservers
```

Re-apply without rebooting:

```bash
sudo /etc/bishos/net-up
```

That is the reason this is a script and not C. Networking is the part of a
machine most likely to need changing on the machine itself, and the old
arrangement -- addresses compiled into `init` -- meant every experiment cost a
recompile, a rebuilt initramfs and a reboot.

Every value here also exists as a default compiled into `init`. If this file
is deleted or broken, the machine still comes up on the network exactly as it
did before the file existed. That duplication is deliberate: a typo should not
cost you the network you would need to fetch the fix.

`init` prints what actually happened, including the nameserver it ended up
with rather than the one it hoped for:

```
Networking: DHCP (DNS: 10.0.2.3)
```

### When a program that is definitely there reports "not found"

Sooner or later something gets installed by `curl ... | sh`, or downloaded as
a release tarball, and BishOS says this:

```
$ ./hello
sh: ./hello: not found
$ ls -la hello
-rwxr-xr-x    1 root     root         70432 hello
```

The file is right there, executable, owned correctly. The message is still
`not found`, and it is not lying -- it is just not talking about the file.

Almost everything prebuilt on the internet is linked against **glibc**, and a
dynamic binary names the loader it needs inside itself:

```
$ readelf -l hello | grep interpreter
[Requesting program interpreter: /lib/ld-linux-aarch64.so.1]
```

BishOS is a musl system. Its loader is `/lib/ld-musl-aarch64.so.1`, so
`/lib/ld-linux-aarch64.so.1` genuinely does not exist -- and when the kernel
cannot find a binary's interpreter it returns `ENOENT`, which the shell
reports about the thing you named rather than the loader you never mentioned.
That is the whole trick: **`not found` means the loader, not the file.**

`readelf -l` is how to confirm it, and the fix is one package:

```bash
sudo apk add gcompat        # 131 KB
```

`gcompat` provides `/lib/ld-linux-*.so` and maps the glibc symbols onto musl.
The same binary then runs:

```
$ ./hello
hello from a glibc binary
```

`gcompat` ships on every persistent root, so in practice the failure above is
one you will only meet on a disk built before this or on a RAM-only boot. It
costs 131 KB against a 5 GB filesystem, and the failure it prevents is the
single most misleading message in Unix -- a bad trade to make anyone debug
twice.

Worth knowing what it is, though: a translation layer that maps glibc symbols
onto musl, not glibc itself. Most things work; something unusual can start
and then misbehave in a way that failing outright would not have hidden. If a
prebuilt binary behaves strangely rather than refusing to run, gcompat
sitting underneath it is worth suspecting early. Anything from the Alpine
repository is unaffected -- those are musl binaries and never touch it.
