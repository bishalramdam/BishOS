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

---

## 1. The three real gaps

These are what separate "an OS I boot" from "a system someone could run".

### 1.1 No authentication
`/etc/passwd` has `x` in the password field but **there is no `/etc/shadow`**,
so that `x` points at nothing. Anyone at the console is root, and
`su - bishal` needs no password. There is also no `login`/`getty` -- init
drops straight into a root shell.

- Create `/etc/shadow` with hashed passwords (`mkpasswd -m sha512`)
- Decide whether init execs `getty` (which runs `login`) instead of `sh -l`

### 1.2 No service management
Init runs exactly one thing: a login shell, respawned forever. There is no
way to say "start this at boot, restart it if it dies". `apk add nginx` and
nothing launches it.

- Read a table of programs to start (an `/etc/inittab`-like file)
- Start each, track pids, restart on exit
- The reap loop already handles the hard part -- it knows which child died

This is the biggest architectural gap and the most interesting thing left:
writing it *is* writing the thing systemd exists to be.

---

## 2. Robustness

### 2.1 No remote access
Console only. `/dev/pts` now exists, so the remaining work is
`apk add openssh`, generating host keys, and a way to start `sshd` at boot
(1.2). Passwords (1.1) matter here too -- sshd will refuse to let root in
without one.

### 2.2 Nothing ever runs fsck
Clean shutdown remounts read-only, which is right, but a power loss or a
killed QEMU leaves ext4 dirty and nothing repairs it. Eventually a root that
will not mount.

- Run `e2fsck -p` on the root device from the initramfs, before mounting

### 2.3 The clock is wrong and nothing fixes it
No RTC read, no NTP. This fails in a *confusing* way: TLS checks certificate
validity dates, so a badly wrong clock makes `apk` and `curl` fail with
certificate errors that look like a CA problem but are not.

- Read the hardware clock at boot (`hwclock -s`)
- Optionally `apk add chrony` or busybox `ntpd` once 1.2 exists

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

**1.2 → 1.1 → 2.1.** (`/dev/pts` is done.)

A minimal service table, then passwords, then sshd. That sequence turns
BishOS from "an OS I boot and type into" into "a machine I can log into that
runs things", and each step is an evening's work.

Worth remembering: this is a learning project, and it is allowed to be
finished. v0.4.0 boots on real hardware and installs Python. Everything above
is a roadmap, not a debt.
