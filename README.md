# BishOS

A minimal Linux system built from the ground up: the **mainline Linux kernel**
plus a userland written from scratch — my own `init` (pid 1) and my own shell.
No BusyBox, no distro, no bootloader magic. It boots in QEMU straight to my
own prompt.

Target: **aarch64** (`qemu-system-aarch64 -machine virt`), so it runs at
near-native speed on Apple Silicon via Hypervisor.framework.

## Status

Starting from zero. Nothing is built yet.

## Plan

- [ ] Toolchain: build the kernel in a Linux container, boot it with QEMU on the host
- [ ] Kernel: `arm64 defconfig` trimmed down, booting to "no init found"
- [ ] `init`: mount `/proc`, `/sys`, `/dev`, reap orphans, spawn a shell, never exit
- [ ] `initramfs`: gzip'd `newc` cpio image with `/init`, `/bin/sh` and the device nodes
- [ ] Shell (`msh`): read → parse → fork → exec, plus builtins (`cd`, `pwd`, `echo`, `exit`)
- [ ] Redirection (`<`, `>`, `>>`), pipes (`|`), background jobs (`&`)
- [ ] Line editing and history (raw-mode terminal handling)
- [ ] Enough coreutils of my own to explore the system (`ls`, `cat`, `ps`, `free`)
- [ ] Clean shutdown via `reboot(2)` / PSCI so `poweroff` exits QEMU
- [ ] Stretch: virtio block device + a real root filesystem instead of initramfs

## Toolchain notes

- The kernel is built inside a **Linux container**, not on macOS directly. The
  Linux source tree contains filenames that differ only by case
  (`netfilter/xt_CONNMARK.h` vs `xt_connmark.h`), so extracting it onto a
  case-insensitive APFS volume corrupts the tree. Keeping the source in a Docker
  volume (ext4) also avoids slow bind-mount I/O on a build with ~30k objects.
- QEMU runs on the **host**, not in the container: `qemu-system-aarch64` with
  `-machine virt,accel=hvf,gic-version=3 -cpu host` gets hardware virtualisation
  because guest and host are both arm64.
- On the `virt` machine the console is a PL011 UART, so the kernel needs
  `console=ttyAMA0` and `-nographic`. Quit QEMU with `Ctrl-A X`.

## Requirements

| Tool | Why | Install |
| --- | --- | --- |
| Docker Desktop | Linux build environment (kernel + userland) | already installed |
| QEMU | boots the result | `brew install qemu` (installed) |
| CLion | editing/indexing the C code | JetBrains Toolbox |

## Building in CLion

CLion is the right JetBrains IDE here — it is the only one that indexes C
natively, and it can compile *inside a Docker container* while you edit on
macOS. (IntelliJ IDEA has no real C support; PyCharm/WebStorm are irrelevant.)

1. **Open the project**: CLion → `Open` → `~/Projects/Personal/BishOS`.
2. **Point it at Linux, not macOS.** The userland calls `mount(2)`,
   `reboot(2)` and includes `<linux/*.h>`, none of which exist on macOS, so a
   native toolchain will not even parse the code:
   `Settings ⌘,` → `Build, Execution, Deployment` → `Toolchains` → `+` →
   **Docker** → pick the build image → CLion auto-detects `gcc`, `make`, `cmake`,
   `gdb` inside it.
3. **CMake profile**: `Settings` → `Build, Execution, Deployment` → `CMake` → `+`
   → set `Toolchain: Docker`, `Build type: MinSizeRel`. That profile's
   `compile_commands.json` is what powers code insight, so `#include <sys/mount.h>`
   resolves and autocomplete works on Linux syscalls.
4. **Run configurations**: add a *Shell Script* configuration per build step
   (kernel, initramfs, boot) so `⌃R` builds and boots without leaving the IDE.
   The QEMU one must run on the host, not in the container.
5. **Debugging the kernel**: boot QEMU with `-s -S` (GDB stub on `:1234`,
   cpu halted), build the kernel with `CONFIG_DEBUG_INFO_DWARF5=y`, then use
   CLion's *GDB Remote Debug* configuration: target `localhost:1234`, symbol
   file `vmlinux`, and set breakpoints in kernel source normally.
6. **Reading kernel source in the IDE**: the tree lives in a Docker volume, so
   it is not visible from macOS. To browse it locally, create a
   **case-sensitive APFS volume** in Disk Utility and extract the tarball there —
   otherwise the checkout is subtly broken.

## Layout (as it grows)

```
kernel/      config fragments, build script
userland/    init/ and msh/ — the parts I actually write
initramfs/   root filesystem staging + cpio packing
scripts/     build + run helpers
```

## Reference

- Linux 6.18 LTS — https://cdn.kernel.org/pub/linux/kernel/v6.x/
- `Documentation/filesystems/ramfs-rootfs-initramfs.rst` in the kernel tree
- `man 2 mount`, `man 2 reboot`, `man 7 signal`, `man 2 waitpid`
- QEMU `virt` machine — https://www.qemu.org/docs/master/system/arm/virt.html
