# BishOS TODO

Where the system stands and what is left. Everything in "Done" boots and is
verified; the last few entries are on `main` and not yet in a tagged release.

Ordered by how much it changes what the OS *is*, not by effort.

---

## Done

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
- Remote access: `sshd` runs from the service table but stays off until
  `/etc/bishos/ssh.enabled` exists, generating its host keys on the machine
  the first time it is actually turned on. Root may not log in over the
  network; `make run` forwards a host port so the guest is reachable
- Service output: init gives every non-console service a pipe and forwards
  what it prints to syslogd under that service's own name, so a daemon that
  never calls syslog(3) still ends up in `/var/log/messages`
- Console on a real terminal: the session runs on the tty named in
  `/sys/class/tty/console/active` rather than `/dev/console`, which is a
  redirector and not a controlling terminal -- so job control works and a
  shell can hand the terminal back instead of failing on the way out
- `make disk-update`: refreshes `init` and `/etc` on an existing disk without
  touching accounts, home directories or installed packages

---

## 1. Considered and declined

### fsck on every boot
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

---

## 2. Polish and learning

Nothing here changes what the OS is. It is a list of small good ideas.

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
  trap, why `/dev/console` is not a terminal a shell can hand back, static vs
  dynamic linking and the musl loader, and the fact that `chown` silently
  drops the setuid bit.
- **`who`, `w` and `uptime` do not see anyone.** `login` records sessions in
  `/var/run/utmp`, and there is no such file, so `uptime` reports "0 users"
  while somebody is sitting at the console and `who` prints nothing at all.
  init already creates `/var/log` on both boot paths; `/var/run` and an empty
  `utmp` beside it is the same one-line fix. Cosmetic, but it is the kind of
  gap that makes a working tool look broken rather than absent.
- **Multiple TTYs.** One console session only.
- **Untested paths.** The x86_64 ISO has never booted on real hardware or
  from a USB stick (the hybrid MBR is meant to support it). The ghcr.io push
  has not been confirmed working.
- **Swap.** None configured.
- **Network config in files.** Addresses and DNS are decided in `init.c`
  rather than read from `/etc`.

---

## Suggested order

**Nothing, in the sense that matters.** There is no open work in section 1:
the machine boots, knows who you are, can be reached over the network,
supervises its services and keeps a record of what they said. Everything left
is polish, and a LICENSE file is the cheapest thing on that list.

The one real gap is not code. The x86_64 ISO has never booted on real
hardware or from a USB stick, and three tagged releases now carry that
untested path -- it is the only claim the README makes that has never been
checked. Everything else here is optional; that one is a promise outstanding.

Worth remembering: this is a learning project, and it is allowed to be
finished. It boots on real hardware, installs Python, supervises services,
keeps its own clock and logs across reboots, and now asks who you are.
Everything above is a roadmap, not a debt.
