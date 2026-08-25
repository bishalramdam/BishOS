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
- `bishos-install`: turns a live boot into an installed one, copying the
  running system onto a BISHOS-labelled partition and giving it a package
  manager from the bundle on the boot media
- A console font large enough to read on a real monitor (`FONT_TER16x32`,
  selected by `fbcon=font:TER16x32`)
- init waits 45 seconds for a slow USB stick to enumerate rather than five,
  printing progress, because a measured stick took 23 seconds to appear
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
- Network settings in `/etc/bishos/network`, applied by `/etc/bishos/net-up`,
  editable and re-runnable on the machine instead of needing a rebuild. The
  old compiled-in path stays in `init` as the fallback, so a broken config
  cannot cost you the network you would need to fix it
- DNS comes from the DHCP server rather than being overwritten with Google's
  on every lease, so a router's own resolver -- and the local names only it
  knows -- actually works
- No username and no hostname in the image. The first boot asks for both,
  defaulting to `bishal` and `BishOS`, and `adduser` creates the account and
  puts it in `wheel`. The prompt uses `\h`, so a renamed machine says its own
  name

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
- **README "what I learned".** There is now enough of this to be the most
  interesting page in the repo: PID 1 signal semantics; `TIOCSCTTY` and job
  control; the `console=` preference trap; why `/dev/console` is not a
  terminal a shell can hand back; why a GPU driver loading can turn the
  screen off; static vs dynamic linking, the musl loader, and why "not found"
  names the wrong thing; that `chown` silently drops the setuid bit; that
  musl has no utmp; that a green CI run described a machine nobody had; and
  that Docker Desktop remaps file ownership on macOS while Linux preserves it,
  so a build that reads root-owned files works on the developer's machine and
  silently drops them everywhere else.
- **Multiple TTYs.** One console session only. Now actually possible: virtual
  terminals need a working display console, which is what the DRM fix
  provides, so this becomes a `console` line per `tty1`..`tty6` in the service
  table rather than something the kernel cannot do.
- **Untested paths.** Only one left: the ghcr.io push has never been confirmed
  working. Everything else has now been seen on real hardware -- an Intel
  desktop booting the x86_64 ISO from a USB stick, with a working screen,
  networking, DHCP-supplied DNS, a persistent root on the stick's second
  partition, and accounts created on first boot.
- **Swap.** None configured.

---

## Suggested order

**Nothing, in the sense that matters.** Section 1 holds only things looked at
and decided against -- there is no open work left in it. The machine boots,
knows who you are, can be reached over the network when asked, supervises its
services and keeps a record of what they said. Everything remaining is
polish, and a LICENSE file is the cheapest thing on that list.

The promise that stood since the beginning is kept: BishOS boots on real
hardware from a USB stick, with a screen, a network, and a root that
remembers. Four faults stood between the tag and that, and none of them could
be reproduced in QEMU -- a GPU driver evicting the console it needed, a
five-second wait for a stick that took twenty-three, an initramfs built on
the host that silently dropped every root-owned file on any machine but the
author's, and an installer that had never existed because nothing had needed
it before.

What is left is optional.

Worth remembering: this is a learning project, and it is allowed to be
finished. It boots on real hardware, installs Python, supervises services,
keeps its own clock and logs across reboots, and now asks who you are.
Everything above is a roadmap, not a debt.
