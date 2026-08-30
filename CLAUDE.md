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

### This machine cannot build with -j4

Two kernel builds died mid-compile, each time as an instant power-off with no
warning. Not a crash and not a clean shutdown -- the power simply went. The
logs showed no thermal warning, no throttling and no machine check, memory was
barely used, and a temperature log through a successful `-j2` build peaked at
43 C against a 100 C limit.

Four cores at 100% for many minutes is the heaviest sustained load this machine
ever sees, and the power supply cannot hold it. Heat is not the mechanism.

**So:** build with `make -j2`. It takes longer and it finishes.

### A power cut leaves object files that are not objects

After those two cuts the tree was full of wreckage, and it failed the build in
two different ways an hour apart:

- 22 objects truncated to zero length. `fixdep` died on a missing `.o.d` while
  a stale `.cmd` claimed the work was done.
- 3 objects with a plausible size and no ELF header, which only surfaced at
  link time as `member arch/x86/kernel/cpu/bugs.o in archive is not an object`.

Scanning for zero-length files finds the first kind and misses the second.
Check the header instead:

```bash
find . -name '*.o' | while read -r f; do
    [ "$(head -c4 "$f" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || echo "$f"
done
```

Delete each one along with its `.cmd` and `.d`, and `vmlinux.a`, `vmlinux.o`
and `built-in.a`, which still name the dead members. Then rebuild. That is far
cheaper than `make clean`, which costs the whole two hours.

### BusyBox date has no %N

`date +%s%N` prints seconds and leaves the `%N` unexpanded, so millisecond
arithmetic collapses to zero. A program that ran for four seconds was measured
as running for 0 ms, and the conclusion drawn was that it exited instantly.
Half an hour went into debugging a program that was working.

**So:** time things with `time`, and distrust a measurement before distrusting
the thing measured.

`coreutils` is installed now, so `/usr/bin/date` is GNU and `%N` works -- but
the lesson stands for anything else BusyBox implements differently, and the
ISO's initramfs still carries only busybox.

### Building a kernel is three commands

```bash
make -j2 bzImage      # the image -- -j2, see above
make -j2 modules      # anything set to =m
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

### The installed system boots without an initramfs; the ISO cannot

An installed root boots straight from the kernel: every driver needed to reach
it is built in, so `root=PARTUUID=... rootwait rw init=/bin/init` mounts the
disk and hands to BusyBox init, which reads `/etc/inittab` and starts OpenRC.
No initramfs is involved.

The ISO still needs one and always will. A live image has no root partition, so
its initramfs *is* the root -- and it is the only place `bishos-install` runs.
`src/init.c` is therefore still built by the Makefile even though nothing on
the installed machine runs it.

Three things that cost time getting there:

- The kernel accepts `root=PARTUUID=` and `root=PARTLABEL=` and nothing else.
  A filesystem `LABEL=` is unreachable: the partition table is read before any
  filesystem driver exists. Finding a root by filesystem label is exactly the
  job an initramfs existed to do.
- An initramfs embedded with `CONFIG_INITRAMFS_SOURCE` and a kernel that mounts
  its own root are mutually exclusive. A built-in initramfs is always unpacked
  and `/init` always runs from it, so `root=` is never reached. Embedding is
  for keeping `init.c` in one file; it is not a step toward removing it.
- Alpine's init scripts drop privileges to users BishOS never created --
  `klogd` wants a `klogd` user, `ntpd` wants `ntp`. Both fail with what reads
  as "not found" and is really a missing account. `ntpd` also has to run as
  root here, because `/etc/bishos/ntp-step` writes the RTC with `hwclock -w`
  and that needs root; under the `ntp` user the write fails silently on a
  `|| true` and the corrected time never reaches the hardware clock.

Alpine ships init scripts in separate `-openrc` subpackages. The base package
contains none, so `apk add openrc` alone leaves `/etc/init.d` almost empty.

### parted counts in GB, resize2fs counts in GiB

Shrinking the 1 TB drive to make room for a root partition, the filesystem was
resized with `resize2fs /dev/sda1 700G` -- 700 **GiB**, 751.6 billion bytes.
The next step was going to be `parted ... resizepart 1 705GB`, which looks like
a safe 5 GB of slack and is not: parted's `GB` is 10^9, so 705GB is 656 GiB.
That is 44 GiB *smaller* than the filesystem, and shrinking a partition below
its filesystem truncates it. The disk held 117 GB of the user's academic work.

Caught only because parted printed `Disk /dev/sda: 1000GB` for a drive every
other tool calls 931 GB.

**So:** the first command inside parted is `unit GiB`. Then every number on
screen matches what resize2fs, lsblk and df report, and the comparison is
direct rather than arithmetic.

Two more things from that migration:

- `resize2fs` prints one line when it starts and one when it finishes, and
  nothing in between. A 916 GB to 700 GB shrink moved 34 GB and took about
  forty minutes in complete silence. Progress is visible only in
  `/sys/block/sda/stat`, where fields 3 and 7 are sectors read and written.
- Shrink order is filesystem first, then partition. Growing is the reverse.
  The rule underneath is that the filesystem must never be larger than the
  partition holding it.

### There is no udev, no logind, no systemd

A desktop assumes all three. What had to be added by hand: `seatd -g seat`
(the `-g` matters, or the socket is root-only), device permissions for
`/dev/dri` and `/dev/input`, `eudev` plus a trigger for hardware that already
existed, `XDG_RUNTIME_DIR`, and the standard system groups.

The absence of **logind** is the one that keeps costing time, because nothing
ever mentions it. polkit decides whether you may do something by asking logind
whether your session is active and local; with no logind it sees no session at
all and refuses. That surfaced as `not authorized` when mounting a disk in the
file manager, and as `Flatpak system operation ConfigureRemote not allowed for
user`. udev has the same hole from the other side: a rule tagging a device
`uaccess` grants the active session user access *through logind*, so those
rules silently do nothing here.

The fix in both cases is to authorise by group instead of by session -- a
polkit rule for `wheel`, and a group named in `etc/bishos/devperms`. Whenever
something says "not authorized" and you are plainly sitting at the machine,
this is why.

### The error almost never names the cause

Five separate failures in one day, none of which said what was wrong:

| What it said | What it was |
| --- | --- |
| `Can't create temporary directory` (flatpak) | `CONFIG_FUSE_FS` unset, so no `/dev/fuse` |
| `AttributeError: 'NoneType' has no attribute 'split'` | `LANG` was not set anywhere on the system |
| `Cannot autolaunch D-Bus without X11 $DISPLAY` | no session bus, on a machine that never ran X |
| `not authorized` (udisks2) | no logind for polkit to ask |
| nothing at all -- apps simply did not launch | `env -S` is GNU, and `/usr/bin/env` was busybox |

The shape is always the same: a minimal system omits something every other
distribution has, and the program that trips over it reports the symptom from
wherever it happened to notice. **Read the error as "something is missing",
not as a description of the missing thing.**

### /etc/profile must source /etc/profile.d

It did not, for months. Packages install snippets there rather than editing
`/etc/profile`, and every distribution's profile sources them -- so three
files sat in `/etc/profile.d` being read by nobody, including the one flatpak
installs to put its exports on `XDG_DATA_DIRS`. Without it, flatpak
applications never appear in any launcher, and the only clue is a warning
during install that reads like advice rather than a fault.

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

### sway splits a config line on ";" before any shell sees it

    exec_always pkill -x matrix-rain; matrix-rain --cell 12 ...

ran `exec_always pkill -x matrix-rain`, which succeeded and did nothing, and
then tried `matrix-rain` as a sway command. `swaymsg` says so plainly --
`{"success": false, "error": "Unknown/invalid command 'matrix-rain'"}` -- but
the wallpaper simply never appeared, which looks like the program failing.
Quote the whole thing so it reaches `sh` as one command.

### A process name is truncated to 15 characters

`matrix-wallpaper` is 16, so it appeared in `/proc` as `matrix-wallpape` and
`pkill -x matrix-wallpaper` matched nothing -- every config reload stacked
another copy. `pkill -f` matches, but it also matches the shell sway runs the
line in and kills the parent before the program starts. Name things so they
fit.

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
