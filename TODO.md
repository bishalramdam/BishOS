# BishOS TODO

Where the system stands after v0.4.0, and what is left. Everything in
"Done" boots and is verified; everything below it is not started.

Ordered by how much it changes what the OS *is*, not by effort.

---

## Done (v0.4.0)

- Linux 6.18.46 LTS compiled from source, pinned by SHA-256, x86_64 + arm64
- Custom C PID 1: mounts pseudo-filesystems, reaps orphans, controlling TTY
  with working job control, graceful signal-driven shutdown
- Persistent ext4 root reached by `switch_root`, with a RAM-only fallback
- DHCP networking with a static fallback; one code path for QEMU, VMware,
  and real routers
- `apk` package manager with signature verification and a CA bundle
- GRUB ISOs for both arches (UEFI, plus legacy BIOS on x86_64)
- CI builds, boot-tests, and publishes both ISOs on tags
- Pseudo-terminals: `devpts` mounted at `/dev/pts` (plus `/dev/shm`), so ssh,
  tmux and screen have ptys to allocate
- Service supervision: init reads `/etc/bishos/services` and starts, tracks
  and restarts what it lists, with backoff for anything that dies at once

---

## 1. The remaining real gap

What separates "an OS I boot" from "a system someone could run".

### 1.1 No authentication
`/etc/passwd` has `x` in the password field but **there is no `/etc/shadow`**,
so that `x` points at nothing. Anyone at the console is root, and
`su - bishal` needs no password. There is also no `login`/`getty` -- init
drops straight into a root shell.

- Create `/etc/shadow` with hashed passwords (`mkpasswd -m sha512`)
- Decide whether init execs `getty` (which runs `login`) instead of `sh -l`

---

## 2. Robustness

### 2.1 No remote access
Console only. `/dev/pts` now exists, so the remaining work is
`apk add openssh`, generating host keys, and a `sshd respawn` line in the
service table. Passwords (1.1) matter here too -- sshd will refuse to let
root in without one.

### 2.2 Nothing ever runs fsck
Clean shutdown remounts read-only, which is right, but a power loss or a
killed QEMU leaves ext4 dirty and nothing repairs it. Eventually a root that
will not mount.

- Run `e2fsck -p` on the root device from the initramfs, before mounting

### 2.3 The clock drifts, and nothing corrects it
Not as bad as first assumed: the kernel already sets the time from the
hardware clock at boot (`CONFIG_RTC_HCTOSYS` + PL031 on arm64, CMOS on
x86_64), which is why TLS works at all -- certificate validation checks
dates, so it would fail outright on a badly wrong clock.

What is missing is *correction*: nothing keeps time in sync, so a long-lived
VM drifts, and a snapshot resumed much later starts wrong. When it bites it
looks like a CA problem rather than a clock problem.

- `apk add chrony`, or busybox `ntpd`, and a `respawn` line in the service
  table -- there is now somewhere to put it

### 2.4 No logging
`dmesg` is the only record and it does not survive a reboot. No syslog.

---

## 3. Polish and learning

- **Minimal kernel config.** Still stock `defconfig` (14 MB x86_64,
  41 MB arm64). `make menuconfig`, cut one subsystem per boot test, then
  `make savedefconfig` and track the result as `config/bishos_defconfig`.
- **LICENSE file.** Missing, and it matters the moment anyone reads the repo.
  MIT or GPL-2.0 (the latter is thematic for a Linux project).
- **`gcompat`.** Prebuilt binaries from the internet are usually glibc-linked
  and fail on musl with a confusing `not found` (that is the missing loader,
  not a missing file). `apk add gcompat` provides a compatibility layer --
  worth documenting, since anything installed by `curl | bash` will hit it.
- **README "what I learned".** The parts other people find interesting: PID 1
  signal semantics, `TIOCSCTTY` and job control, the `console=` preference
  trap, static vs dynamic linking and the musl loader.
- **Multiple TTYs.** One console session only.
- **Untested paths.** The x86_64 ISO has never booted on real hardware or
  from a USB stick (the hybrid MBR is meant to support it). The ghcr.io push
  has not been confirmed working.
- **Swap.** None configured.
- **Network config in files.** Addresses and DNS are decided in `init.c`
  rather than read from `/etc`.

---

## Suggested order

**1.1 → 2.1.** (`/dev/pts` and the service table are done.)

Passwords, then sshd. Between them that turns BishOS from "an OS I boot and
type into" into "a machine I can log into that runs things", and the pieces
they need -- ptys and something to keep a daemon running -- now exist.

Worth remembering: this is a learning project, and it is allowed to be
finished. v0.4.0 boots on real hardware and installs Python. Everything above
is a roadmap, not a debt.
