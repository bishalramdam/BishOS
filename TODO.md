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
- `gcompat` on every persistent root, so prebuilt glibc binaries run instead
  of failing with `not found` for a file that is plainly there. Documented
  alongside `readelf -l`, which is how to recognise the failure elsewhere
- A display console that survives the GPU driver loading: `DRM_FBDEV_EMULATION`
  and friends, without which `i915` evicts the firmware framebuffer on a real
  PC and replaces it with nothing. Boot menu gained `nomodeset` and verbose
  entries to tell a graphics fault from any other kind
- Finished ISOs in `output/<arch>/`, left alone by `make clean`
- An unknown `ARCH` is rejected before anything runs, rather than falling
  through to x86_64 and failing much later inside the kernel's own Makefile

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

### `who` and `uptime` reporting who is logged in -- not possible here
`uptime` says "0 users" with somebody sitting at the console, and `who`
prints nothing. The obvious cause is that `/var/run/utmp` does not exist, and
the obvious fix is to create it next to `/var/log`. That fix does not work,
and it is worth writing down why so it is not attempted twice.

**musl does not implement utmp.** `pututxline()` returns NULL and writes
nothing; `getutxent()` returns NULL however the file is populated. Both were
checked directly, and so was the stronger case: a byte-perfect
`USER_PROCESS` record written into `/var/run/utmp` by hand still produces no
output from `who`, because busybox reads through those same stubs rather than
parsing the file itself.

So this needs either glibc, or a busybox patched to read the file directly --
both far out of proportion to `who` working. Note also that `w` and `users`
are not in this busybox build at all, so only `who`, `last` and `uptime`'s
user count are affected.

---

## 2. Polish and learning

Nothing here changes what the OS is. It is a list of small good ideas.

- **Minimal kernel config.** Still stock `defconfig` (14 MB x86_64,
  41 MB arm64). `make menuconfig`, cut one subsystem per boot test, then
  `make savedefconfig` and track the result as `config/bishos_defconfig`.
  Worth more than size: the blank-screen bug lived in a 5,525-line `.config`
  that exists only inside a Docker volume, regenerated from upstream defaults
  every build and never seen. Tracking it would have made that bug a diff.
- **LICENSE file.** Missing, and it matters the moment anyone reads the repo.
  MIT or GPL-2.0 (the latter is thematic for a Linux project).
- **README "what I learned".** The parts other people find interesting: PID 1
  signal semantics, `TIOCSCTTY` and job control, the `console=` preference
  trap, why `/dev/console` is not a terminal a shell can hand back, static vs
  dynamic linking and the musl loader, and the fact that `chown` silently
  drops the setuid bit.
- **Multiple TTYs.** One console session only. Now actually possible: virtual
  terminals need a working display console, which is what the DRM fix
  provides, so this becomes a `console` line per `tty1`..`tty6` in the service
  table rather than something the kernel cannot do.
- **Untested paths.** The x86_64 ISO has been tried on real hardware and the
  screen stayed black -- diagnosed as the DRM console gap and fixed, but the
  fix itself has not been confirmed on the machine yet. The ghcr.io push has
  not been confirmed working either.
- **Swap.** None configured.
- **Network config in files.** Addresses and DNS are decided in `init.c`
  rather than read from `/etc`.

---

## Suggested order

**Nothing, in the sense that matters.** Section 1 holds only things looked at
and decided against -- there is no open work left in it. The machine boots,
knows who you are, can be reached over the network when asked, supervises its
services and keeps a record of what they said. Everything remaining is
polish, and a LICENSE file is the cheapest thing on that list.

The one real gap is confirmation, not code. The x86_64 ISO was tried on an
Intel desktop and the screen stayed black after GRUB; every release up to
0.8.0 carries that. The cause is understood and fixed -- a GPU driver built
in without a console to replace the firmware framebuffer it evicts -- but the
fix has not yet been run on the machine that showed the fault, and QEMU
cannot show it either way. Everything else here is optional; that one is a
promise outstanding.

Worth remembering: this is a learning project, and it is allowed to be
finished. It boots on real hardware, installs Python, supervises services,
keeps its own clock and logs across reboots, and now asks who you are.
Everything above is a roadmap, not a debt.
