# BishOS 🐧

[![Build ISOs](https://github.com/bishalramdam/BishOS/actions/workflows/build-iso.yml/badge.svg)](https://github.com/bishalramdam/BishOS/actions/workflows/build-iso.yml)

A minimal Linux operating system built modularly like **Lego blocks**: combining the mainline **Linux kernel** (compiled from source) with custom and open-source userspace components (`/init`, shell, standard utilities) to learn operating system fundamentals from the ground up.

Builds for **two architectures** from the same source:

| | x86_64 (default) | arm64 |
| --- | --- | --- |
| Kernel image | `bzImage` | `Image` |
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
| 3. Userspace Init & Shell                                   |
|    - Phase 1: Minimal static C init (PID 1 heartbeat)       |
|    - Phase 2: BusyBox static tools + interactive shell (sh) |
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

2. **Boot in QEMU**:
   ```bash
   make run              # x86_64 (software-emulated on Apple Silicon)
   make ARCH=arm64 run   # arm64 (near-native speed on Apple Silicon)
   ```

3. **Exit QEMU**:
   Press `Ctrl + A`, release, then press `X`.

4. **Build a bootable ISO**:
   ```bash
   make ARCH=arm64 iso   # UEFI/GRUB  -> build/arm64/bishos-arm64.iso
   make iso              # BIOS/isolinux -> build/x86_64/bishos-x86_64.iso
   ```
   Prebuilt ISOs for both architectures are also attached to every
   [CI run](https://github.com/bishalramdam/BishOS/actions) and to
   [tagged releases](https://github.com/bishalramdam/BishOS/releases).
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
  - [x] BIOS-bootable x86_64 ISO (isolinux, hybrid MBR for CD **and** USB)
  - [x] CI builds and boot-tests both ISOs on native runners, publishes
        them on tagged releases

---

## 📂 Project Layout

```
├── .github/workflows/
│   └── build-iso.yml   # CI: builds + boot-tests both ISOs, publishes releases
├── Makefile            # Build and run automation (ARCH=x86_64 | arm64)
├── Dockerfile.kernel   # Kernel build container (toolchains + GRUB/xorriso for ISOs)
├── grub/
│   └── grub.cfg        # UEFI ISO boot menu (arm64)
├── isolinux/
│   └── isolinux.cfg    # BIOS ISO boot menu (x86_64)
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
│       └── bishos-arm64.iso
└── README.md
```
