# BishOS TODO

Where the system stands after v0.7.1, and what is left. Everything in
"Done" boots and is verified; everything below it is not started.

Ordered by how much it changes what the OS *is*, not by effort.

---

## Done (v0.7.1)

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
- Clock correction: busybox `ntpd` runs as a service and writes the corrected
  time back to the hardware clock, so the next boot starts right
- Logging: `syslogd` and `klogd` run as services, so the kernel log lands in
  `/var/log/messages` on the persistent root and outlives a reboot
- Accounts: `/etc/shadow` ships locked, the first boot on a persistent root
  asks for the two passwords, and the console runs `login` rather than a bare
  root shell. `sudo` on the disk gives `wheel` a way back to root
- Remote access: `sshd` runs from the service table, generating its host keys
  on the machine at first start. Root may not log in over the network;
  `make run` forwards a host port so the guest is reachable

---

## 1. Robustness

### 1.1 Nothing ever runs fsck -- considered and declined
Less urgent than it looks: ext4 journals, and the kernel replays that journal
at mount, so the ordinary power-loss case already repairs itself. What fsck
would add is catching structural damage the journal cannot cover, and running
the periodic check ext4 otherwise never triggers.

Declined on size. There is no `e2fsck` to be had cheaply: busybox ships only
the `fsck` wrapper, Alpine's `e2fsprogs-static` is static *libraries* and no
binary, and upstream e2fsprogs does not build against musl unpatched. The one
route that works -- copying Alpine's dynamic `e2fsck` with its seven shared
libraries and the musl loader -- costs about 870KB compressed, which would
roughly double a 1.1MB initramfs that is loaded into RAM in full on every
boot. Not worth it for a check the journal already covers.

### 1.2 Service output is not logged
`syslogd` and `klogd` now persist the kernel log, but syslog only ever sees
messages a program deliberately sends it. Services started from the table
inherit init's stdout, which is the console -- so their output, and init's
own `[BishOS]` lines, scroll past and are gone.

- Give each non-console service its stdout and stderr on a pipe, and have
  init forward what it reads there into syslog

---

## 2. Polish and learning

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

**1.2, then whatever you feel like.** Section 1 is down to one real item:
services still log nowhere, which is the thing you notice the first time
something breaks while you are not watching. After that the list is polish,
and a LICENSE file is the cheapest thing on it.

The x86_64 ISO has still never booted on real hardware, and two tagged
releases now carry that untested path. It is the only claim in the README
that has never been checked.

Worth remembering: this is a learning project, and it is allowed to be
finished. It boots on real hardware, installs Python, supervises services,
keeps its own clock and logs across reboots, and now asks who you are.
Everything above is a roadmap, not a debt.
