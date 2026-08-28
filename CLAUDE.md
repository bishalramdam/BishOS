# Working on BishOS

A Linux distribution built from source: a kernel compiled from kernel.org, a
PID 1 written in C, BusyBox, and Alpine's `apk` on a persistent ext4 root. It
boots on real hardware from a USB stick and installs itself.

This file is instructions for Claude Code. The general principles are adapted
from [andrej-karpathy-skills](https://github.com/multica-ai/andrej-karpathy-skills);
everything under "What this project has taught" is specific to here and was
learned the expensive way.

---

## The four principles

**1. Think before coding.** Don't assume, don't hide confusion, surface
tradeoffs. State assumptions out loud. If two readings of a request lead to
different work, ask rather than pick silently.

**2. Simplicity first.** The minimum that solves the problem. No speculative
features, no abstractions for one caller, no error handling for cases that
cannot occur.

**3. Surgical changes.** Touch only what the task requires. Don't reformat,
don't refactor working code, don't tidy unrelated things. Match the
surrounding style.

**4. Goal-driven execution.** Decide what "done" means before starting, then
verify it. "It compiles" is not verification.

---

## What this project has taught

These are not general advice. Each one cost real time here.

### QEMU cannot prove this works

The most expensive bugs in this project were invisible in QEMU and obvious on
hardware:

- `DRM_I915` built in without `DRM_FBDEV_EMULATION` blanks the screen on any
  Intel machine. QEMU has no Intel GPU, so `i915` never probes and CI passed
  for four releases while the ISO was unusable.
- A USB stick took 23 seconds to enumerate. init waited 5, a figure measured
  against a virtio disk that appears instantly.

**So:** never say a hardware-facing change works because QEMU booted. Say what
was verified and on what. The untested path is the interesting one.

### Verify what is in the image, not what you put in it

`cpio` reports a file it cannot read, carries on, and exits 0. The initramfs
was built on the host while the rootfs is built as root inside Docker, so
`/etc/shadow` (600), `/etc/sudoers.d/wheel` (440) and `/root` (700) were
silently dropped on Linux and included on macOS — where it was developed.
Every released ISO for five versions had no `/etc/shadow`, which meant root
ended up with an empty password.

**So:** `REQUIRED_IN_INITRAMFS` in the Makefile asserts the archive's contents
after building. Add to it rather than trusting a copy step.

### Check that a kernel option actually stuck

`./scripts/config -e NAME` uppercases the symbol (`tr a-z A-Z`, line 66). Fine
for `CONFIG_TUN`, silently wrong for `CONFIG_FONT_TER16x32`, whose lowercase
`x` names a symbol that does not exist. No error, nothing set.

Options are written straight into `.config` now. After `make olddefconfig`,
always `grep '^CONFIG_X=' .config`. An option whose dependencies are unmet is
dropped without a word.

### A kernel built by hand drifts from the one the Makefile builds

`make kernel` builds inside Docker from upstream `defconfig` plus the symbols
in `KERNEL_CONFIG_ENABLE`. A kernel compiled by hand in `~/kernel` does not go
through any of that, so options set there exist nowhere the repository can see
them.

Eight had accumulated that way. The one that mattered was
`SND_HDA_CODEC_HDMI`: upstream defconfig does not set it, so every ISO this
Makefile produced had no HDMI audio codec, on a machine whose only speakers
are in the monitor. That failure does not look like a kernel problem -- it
looks like PipeWire, or the mixer, or the monitor lying about its ELD, all of
which have been suspected here before for other reasons.

**So:** after configuring a kernel by hand, diff it against what the Makefile
would produce before assuming the two agree:

```bash
cd ~/kernel/linux-<version> && make savedefconfig
comm -13 <(sort arch/x86/configs/x86_64_defconfig) <(sort defconfig)
```

Anything in that output and not in `KERNEL_CONFIG_ENABLE` is a difference that
will vanish the next time an ISO is built.

### Building a kernel is three commands

```bash
make -j4 bzImage      # the image
make -j4 modules      # anything set to =m
sudo make modules_install
```

`make bzImage` alone does not build modules, and `modules_install` then fails
with `No rule to make target 'modules.order'`.

### The disk is not rebuilt

`make disk` deliberately skips an existing image so it cannot destroy data.
The cost is that a machine in daily use keeps booting whatever `init` it was
created with, and new features simply do not appear -- which looks exactly
like new features being broken. Twice this was diagnosed as a bug that did not
exist. `make disk-update` refreshes `init` and `/etc` without touching
accounts or installed packages.

### init reads the service table once

Editing `/etc/bishos/services` changes nothing until reboot. init parses it at
boot and remembers what it parsed.

### BusyBox and musl are not GNU and glibc

Assume nothing is present until checked:

- No `sudo` applet, no `chsh`, no `e2fsck`.
- `wget` cannot be told to prefer IPv4. Use `curl -4`.
- `setfont` exits 0 when the font file does not exist.
- musl has no `utmp`, so `who` and `w` cannot work at all.
- Prebuilt binaries from the internet are glibc-linked and fail with
  `not found` -- which names the missing loader, not the missing file.

### There is no udev, no logind, no systemd

A desktop assumes all three. What had to be added by hand: `seatd -g seat`
(the `-g` matters, or the socket is root-only), device permissions for
`/dev/dri` and `/dev/input`, `eudev` plus a trigger for hardware that already
existed, `XDG_RUNTIME_DIR`, and the standard system groups. None of it is
error-prone once known and all of it is invisible until something fails.

### /run must be a tmpfs, and PATH must include /usr/local

Two gaps found only once a desktop was running on the machine.

`/run` was on disk, so sockets outlived the sessions that made them: eleven
dead sway IPC sockets from six sessions. Anything picking "the" socket by
globbing the directory then talks to a corpse -- and that fails by returning
nothing rather than erroring, so it reads as "sway has no windows" rather than
"you are asking a sway that no longer exists". Three diagnostic passes were
wasted on it. init mounts it as tmpfs now.

`/etc/profile` set `PATH=/bin:/sbin:/usr/bin:/usr/sbin`, with no `/usr/local`.
Anything compiled on the machine was invisible unless typed as a full path.

### Hardware lies

The monitor reports `eld_valid 0` over HDMI -- "I have no audio" -- and plays
sound perfectly. PipeWire believed it and switched the card off. Treat
capability reports as evidence, not proof; test the thing itself.

---

## Conventions

**Commit messages explain why, not what.** The diff shows what changed. The
message should say what was wrong, why this fixes it, and what was verified.
Several bugs here were found by reading old commit messages.

**TODO.md records decisions, not just tasks.** Things considered and rejected
stay, with the reasoning -- `fsck` (870 KB in a 1.1 MB initramfs) and `utmp`
(musl does not implement it). That is what stops them being reattempted.

**Version numbers are load-bearing.** `VERSION` in the Makefile names the ISO
and is compiled into init's banner, and CI fails if a tag disagrees with it.

**Ask before destroying.** Disk images hold real work. `disk-reset` wipes;
`disk-update` does not.

---

## Layout

| Path | What it is |
| --- | --- |
| `src/init.c` | PID 1: mounts, `switch_root`, service supervision, logging |
| `etc/bishos/services` | The service table |
| `etc/bishos/*` | Scripts init runs: console, network, sshd, install |
| `config/bishos_*_defconfig` | Snapshot of the running kernel's config. A record, not build input -- `make kernel` builds from upstream defconfig plus `KERNEL_CONFIG_ENABLE` |
| `Makefile` | Everything. Docker for reproducibility |
| `tools/` | Small utilities that are not part of the OS |
