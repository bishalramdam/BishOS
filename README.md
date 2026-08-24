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
   never overwritten by a rebuild. To wipe it and start clean:
   ```bash
   make ARCH=arm64 disk-reset
   ```

4. **Build a bootable ISO** -- GRUB on both architectures, same layout and
   the same `grub.cfg`; only the kernel binary differs:
   ```bash
   make ARCH=arm64 iso   # -> build/arm64/bishos-0.3.0-arm64.iso   (UEFI)
   make iso              # -> build/x86_64/bishos-0.3.0-x86_64.iso (UEFI + BIOS)
   ```
   `VERSION` in the Makefile is the single source of truth: it names the ISO
   and is compiled into init's banner, so a booted system always reports the
   version of the image it came from.
   Prebuilt ISOs for both architectures are attached to every
   [CI run](https://github.com/bishalramdam/BishOS/actions) and to
   [tagged releases](https://github.com/bishalramdam/BishOS/releases), and
   published to GitHub Packages as OCI artifacts:
   ```bash
   oras pull ghcr.io/bishalramdam/bishos:0.3.0-arm64
   oras pull ghcr.io/bishalramdam/bishos:0.3.0-x86_64
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
- [x] **Phase 7: Persistent Root Filesystem**
  - [x] Raw ext4 disk image built with `mke2fs -d` (no loop device, no
        privileged container)
  - [x] `switch_root` out of the initramfs into the real root, carrying
        `/proc`, `/sys`, `/dev` across with `MS_MOVE`
  - [x] Graceful fallback to running from RAM when no disk is attached
        (so the ISO and direct-kernel boots still work)
  - [x] Read-only remount on shutdown so ext4 stays clean
  - [ ] Package manager on top of the persistent root (apk or apt --
        see Notes)
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

### Why there is no `apt`

BishOS builds its userland against **musl** (Alpine's toolchain), while every
Debian `.deb` -- including `apt` and `dpkg` themselves -- is compiled against
**glibc** and expects perl, a `/var/lib/dpkg` database, and Debian's
filesystem layout. Running real `apt` therefore means replacing the whole
userland with Debian's, which would make BishOS "Debian with a custom kernel
and init" rather than a system built from parts.

The natural fit for this userland is Alpine's **`apk`**: musl-native, a ~1 MB
static binary, and it speaks to the same Alpine mirrors the build already
uses for BusyBox. Now that the root filesystem is persistent, either option
is finally possible -- packages installed on a RAM-only initramfs would have
disappeared at the next reboot.
