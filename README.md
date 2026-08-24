# BishOS 🐧

A minimal Linux operating system built modularly like **Lego blocks**: combining the mainline **Linux kernel** with custom and open-source userspace components (`/init`, shell, standard utilities) to learn operating system fundamentals from the ground up.

---

## 🧱 The Lego Block Architecture

```
+-------------------------------------------------------------+
| 1. The Linux Kernel (bzImage / vmlinuz)                     |
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

1. **Build everything** (compiles `init`, builds `initramfs`, fetches kernel):
   ```bash
   make all
   ```

2. **Boot in QEMU**:
   ```bash
   make run
   ```

3. **Exit QEMU**:
   Press `Ctrl + A`, release, then press `X`.

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
- [ ] **Phase 3: Kernel Compilation from Source**
  - [ ] Download mainline kernel source (`kernel.org`)
  - [ ] Custom minimal `defconfig`
- [ ] **Phase 4: Networking & Advanced Features**
  - [ ] DHCP networking via `udhcpc`
  - [ ] Bootable ISO / disk image generation

---

## 📂 Project Layout

```
├── Makefile            # Build and run automation
├── src/
│   └── init.c          # Minimal C init (PID 1)
├── build/              # Generated build artifacts (gitignored)
│   ├── bzImage         # Linux kernel binary
│   ├── rootfs/         # Root filesystem staging area
│   └── initramfs.cpio.gz
└── README.md
```
