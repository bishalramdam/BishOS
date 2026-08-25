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

3. **Exit QEMU**:
   Press `Ctrl + A`, release, then press `X`.

   The disk at `build/$ARCH/bishos-disk.img` persists across boots and is
   never overwritten by a rebuild. It defaults to 5 GB and is sparse, so it
   costs host disk only as it fills.
   ```bash
   make ARCH=arm64 disk-grow                 # bigger, keeping your data
   make ARCH=arm64 DISK_SIZE=20G disk-grow   # any size you like
   make ARCH=arm64 disk-reset                # wipe and start clean
   ```
   Growing works in place because the image is a whole-disk ext4 filesystem
   with no partition table -- there is no partition to move before resizing.

### Persistent live USB

init looks for its root on *any* block device -- whole disks and partitions
alike -- and adopts the first ext4 filesystem labelled `BISHOS`. That is the
whole mechanism, so a live USB gains persistence by giving it such a
partition. On a Linux machine, with `/dev/sdX` the stick:

```bash
sudo wipefs -a /dev/sdX                      # clear old signatures
sudo dd if=bishos-x86_64_v0.6.2.iso of=/dev/sdX bs=4M conv=fsync
sudo sgdisk -e /dev/sdX                      # see below
sudo sgdisk -n 0:0:0 -t 0:8300 /dev/sdX      # claim the free space
sudo mkfs.ext4 -L BISHOS /dev/sdX3           # check the real partition name first
```

`sgdisk -e` is the non-obvious step. The ISO's GPT declares the disk to end
where the image ends (~49 MB), so the rest of the stick sits outside the
partition table and no tool offers it as free space until the backup header
is moved to the true end of the device.

The label must be exactly `BISHOS` -- it is compared with `strcmp`. Anything
else is ignored, which is what stops BishOS from adopting the internal drive
of whatever machine it is booted on.

   The VM gets 2 GB of RAM by default (`make MEMORY=4G run` to change it).
   That figure also sizes `/tmp`: it is a tmpfs, so it defaults to half of
   RAM and lives *in* RAM. If an installer dies with `No space left on
   device` while `df -h /` shows plenty free, `/tmp` is what filled up --
   either raise `MEMORY` or point the installer at disk with
   `TMPDIR=/var/tmp`.

4. **Build a bootable ISO** -- GRUB on both architectures, same layout and
   the same `grub.cfg`; only the kernel binary differs:
   ```bash
   make ARCH=arm64 iso   # -> build/arm64/bishos-arm64_v0.6.2.iso   (UEFI)
   make iso              # -> build/x86_64/bishos-x86_64_v0.6.2.iso (UEFI + BIOS)
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

6. **Install software** -- the persistent root ships with `apk`:
   ```bash
   apk add python3          # or gcc, git, vim, curl, tmux, ...
   python3 --version
   ```
   Installed packages survive reboots, because the root is a real disk.
   Prebuilt ISOs for both architectures are attached to every
   [CI run](https://github.com/bishalramdam/BishOS/actions) and to
   [tagged releases](https://github.com/bishalramdam/BishOS/releases), and
   published to GitHub Packages as OCI artifacts:
   ```bash
   oras pull ghcr.io/bishalramdam/bishos:0.6.2-arm64
   oras pull ghcr.io/bishalramdam/bishos:0.6.2-x86_64
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
- [x] **Phase 8: Package Manager**
  - [x] Alpine's `apk` (static binary) on the persistent root, with Alpine's
        signing keys so downloads are verified, not blindly trusted
  - [x] Repositories pinned to a specific Alpine release
  - [x] CA certificate bundle shipped, so HTTPS can actually be verified
  - [x] Installs dynamically-linked software: the first package pulls in
        `musl`, which provides the loader the whole repository needs
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

## 📂 Project Layout

```
├── .github/workflows/
│   └── build-iso.yml   # CI: builds + boot-tests both ISOs, publishes releases
├── Makefile            # Build and run automation (ARCH=x86_64 | arm64)
├── Dockerfile.kernel   # Kernel build container (native gcc + x86_64 cross-toolchain)
├── Dockerfile.iso      # ISO packaging container (GRUB + xorriso, per-arch)
├── grub/
│   └── grub.cfg        # Boot menu -- one config, every architecture
├── etc/                # System configuration overlay
│   ├── passwd          # User account database (root, bishal)
│   ├── group           # Group definitions
│   ├── hostname        # System hostname (BishOS)
│   ├── hosts           # Local loopback resolution
│   ├── profile         # Login shell environment, aliases, colors
│   ├── resolv.conf     # Google DNS configuration (8.8.8.8)
│   └── udhcpc/
│       └── default.script # DHCP event handler
├── src/
│   └── init.c          # Minimal C init (PID 1 + network auto-bringup)
├── build/              # Generated build artifacts (gitignored)
│   ├── x86_64/
│   │   ├── bzImage     # Compiled Linux kernel (x86_64)
│   │   ├── rootfs/     # Root filesystem staging area
│   │   └── initramfs.cpio.gz
│   └── arm64/
│       ├── Image       # Compiled Linux kernel (arm64)
│       ├── rootfs/
│       ├── initramfs.cpio.gz
│       ├── bishos-disk.img  # Persistent ext4 root (survives rebuilds)
│       └── bishos-arm64.iso
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
