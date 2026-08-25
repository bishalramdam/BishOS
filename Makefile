# Target architecture: x86_64 (default) or arm64.
#   make ARCH=arm64 run   -> native on Apple Silicon, hardware-accelerated via HVF
#   make run              -> x86_64, software-emulated (TCG)
ARCH ?= x86_64

# Single source of truth for the version: compiled into init's banner and
# used in ISO filenames, so a release artifact always names what it is.
VERSION = 0.11.0

# Alpine release the package manager installs from. Pinned like the kernel:
# "latest-stable" would silently change the package set over time.
ALPINE_RELEASE = v3.24

BUILD_DIR = build/$(ARCH)
ROOTFS_DIR = $(BUILD_DIR)/rootfs
INITRAMFS = $(BUILD_DIR)/initramfs.cpio.gz
DISK = $(BUILD_DIR)/bishos-disk.img
DISK_SIZE = 5G

# RAM for the VM. Note this also sizes /tmp, which is a tmpfs and therefore
# defaults to half of RAM -- 256M of RAM left /tmp at 128M, too small for
# installers that unpack a bundled runtime there.
MEMORY = 2G

# Kernel source pin -- bump both together (hash from cdn.kernel.org sha256sums.asc)
KERNEL_VERSION = 6.18.46
KERNEL_SHA256 = f5d44b93808b02cc2969c5404ba081d97523719c9fd2ba2de6db318b4141cca0

# Options defconfig does not set that we need, all of them about one thing:
# whether anything is on the screen after GRUB hands over.
#
# A monitor receives pixels, not letters, so showing text means the kernel
# must own a framebuffer and render glyphs into it. defconfig on x86_64 gives
# only the legacy VGA text console, which does not exist under UEFI, so the
# first four options provide a framebuffer console instead.
#
# The last three are the ones that matter on a real PC, and they are why this
# was broken for four releases while every CI run passed. defconfig builds
# DRM_I915 in, and a DRM driver evicts the firmware framebuffer when it
# probes -- it has to, two drivers cannot own one GPU. Without
# DRM_FBDEV_EMULATION it then provides no console of its own, so on any
# machine with Intel graphics the screen lights up, i915 loads, and it goes
# black. QEMU has no Intel GPU, i915 never probes, efifb is never evicted,
# and the fault is invisible to every test we run.
#
# SIMPLEDRM and SYSFB_SIMPLEFB make the early framebuffer a DRM device too,
# so the handoff to i915 is between two drivers of the same kind rather than
# an eviction, which is what current distributions ship.
#
# Written straight into .config rather than through scripts/config, which
# uppercases every symbol name it is given (`tr a-z A-Z`, line 66). That is
# harmless for options that are already uppercase and silently wrong for
# FONT_TER16x32, whose lowercase x became an X naming a symbol that does not
# exist -- no error, nothing set, and a kernel that then ignores
# fbcon=font:TER16x32 because it has no such font. olddefconfig resolves the
# dependencies afterwards either way.
#
# FONTS and FONT_TER16x32 exist because the console is unreadable otherwise.
# defconfig builds only the 8x8 and 8x16 fonts, and 8x16 on a 1080p monitor is
# 120 columns of very small text -- fine in a terminal window, punishing on a
# screen across a desk. Terminus 16x32 is twice the size in both directions,
# selected by fbcon=font:TER16x32 on the command line. It has to be compiled
# in: changing fonts at runtime needs font *files*, which a system this small
# does not carry.
KERNEL_CONFIG_ENABLE = FB FB_EFI FB_VESA FRAMEBUFFER_CONSOLE \
                       FRAMEBUFFER_CONSOLE_DETECT_PRIMARY \
                       DRM_FBDEV_EMULATION DRM_SIMPLEDRM SYSFB_SIMPLEFB \
                       FONTS FONT_TER16x32

# Reject anything that is not one of the two, before a single command runs.
# An unknown ARCH used to fall through to the x86_64 branch below, so
# "make ARCH=arm65" cheerfully built an x86_64 kernel into build/arm65 and
# died minutes later inside the kernel's own Makefile with "arch/arm65/
# Makefile: No such file or directory" -- which points at Linux rather than
# at the typo that caused it.
ifeq ($(filter $(ARCH),x86_64 arm64),)
$(error ARCH must be x86_64 or arm64, not '$(ARCH)')
endif

ifeq ($(ARCH),arm64)
KERNEL = $(BUILD_DIR)/Image
KERNEL_ARTIFACT = arch/arm64/boot/Image
KERNEL_IMAGE_TARGET = Image
# native arm64 container: no cross-compiler needed
KMAKE = make O=/kbuild/build-arm64 ARCH=arm64
DOCKER_PLATFORM = linux/arm64
QEMU = qemu-system-aarch64 -machine virt -accel hvf -cpu host
CONSOLE = ttyAMA0
NIC_MODEL = virtio-net-pci
else
KERNEL = $(BUILD_DIR)/bzImage
KERNEL_ARTIFACT = arch/x86/boot/bzImage
KERNEL_IMAGE_TARGET = bzImage
KMAKE = make O=/kbuild/build-x86_64 ARCH=x86_64 CROSS_COMPILE=x86_64-linux-gnu-
DOCKER_PLATFORM = linux/amd64
QEMU = qemu-system-x86_64
CONSOLE = ttyS0
NIC_MODEL = e1000
endif

.PHONY: all clean rootfs initramfs kernel run iso disk disk-reset disk-grow disk-update clean-output print-kernel-version print-version

all: kernel rootfs initramfs

# apk.static and Alpine's signing keys are staged for the ISO rather than the
# initramfs. The installer needs them to give a freshly installed root a
# package manager, and at 5MB they would triple an initramfs that is loaded
# into RAM in full on every boot. On the ISO they cost nothing but disc space.
#
# 1. Compile C init (PID 1) and install BusyBox + symlinks + user profiles +
# the CA certificate bundle (without it nothing can verify an HTTPS server:
# apk cannot fetch packages and wget cannot fetch https:// URLs)
rootfs:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$$PWD":/work -w /work alpine:latest sh -c "\
		apk add --no-cache gcc musl-dev busybox-static ca-certificates && \
		rm -rf /work/$(ROOTFS_DIR) && \
		mkdir -p /work/$(ROOTFS_DIR)/bin /work/$(ROOTFS_DIR)/sbin \
		         /work/$(ROOTFS_DIR)/proc /work/$(ROOTFS_DIR)/sys \
		         /work/$(ROOTFS_DIR)/dev /work/$(ROOTFS_DIR)/root \
		         /work/$(ROOTFS_DIR)/home/bishal /work/$(ROOTFS_DIR)/tmp \
		         /work/$(ROOTFS_DIR)/etc /work/$(ROOTFS_DIR)/usr/share/udhcpc \
		         /work/$(ROOTFS_DIR)/etc/ssl/certs /work/$(ROOTFS_DIR)/var/log \
		         /work/$(ROOTFS_DIR)/var/empty && \
		gcc -static -O2 -DBISHOS_VERSION=$(VERSION) /work/src/init.c -o /work/$(ROOTFS_DIR)/init && \
		cp /bin/busybox.static /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod +x /work/$(ROOTFS_DIR)/bin/busybox && \
		cd /work/$(ROOTFS_DIR)/bin && \
		for app in \$$(./busybox --list); do ln -sf busybox \$$app; done && \
		cd /work/$(ROOTFS_DIR)/sbin && \
		for app in \$$(../bin/busybox --list); do ln -sf /bin/busybox \$$app; done && \
		cp -r /work/etc/* /work/$(ROOTFS_DIR)/etc/ && \
		cp /work/etc/udhcpc/default.script /work/$(ROOTFS_DIR)/usr/share/udhcpc/default.script && \
		cp /etc/ssl/certs/ca-certificates.crt /work/$(ROOTFS_DIR)/etc/ssl/certs/ca-certificates.crt && \
		cp /etc/ssl/certs/ca-certificates.crt /work/$(ROOTFS_DIR)/etc/ssl/cert.pem && \
		find /work/$(ROOTFS_DIR) -type d -exec chmod 755 {} + && \
		find /work/$(ROOTFS_DIR)/etc -type f -exec chmod 644 {} + && \
		chmod 755 /work/$(ROOTFS_DIR)/etc/udhcpc/default.script /work/$(ROOTFS_DIR)/usr/share/udhcpc/default.script && \
		chmod 755 /work/$(ROOTFS_DIR)/etc/bishos/ntp-step /work/$(ROOTFS_DIR)/etc/bishos/console && \
		chmod 600 /work/$(ROOTFS_DIR)/etc/shadow && \
		chmod 440 /work/$(ROOTFS_DIR)/etc/sudoers.d/wheel && \
		chmod 755 /work/$(ROOTFS_DIR)/etc/bishos/sshd /work/$(ROOTFS_DIR)/etc/bishos/net-up \
		          /work/$(ROOTFS_DIR)/etc/bishos/install && \
		ln -sf /etc/bishos/install /work/$(ROOTFS_DIR)/sbin/bishos-install && \
		chmod 755 /work/$(ROOTFS_DIR)/init /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod 700 /work/$(ROOTFS_DIR)/root && \
		chmod 1777 /work/$(ROOTFS_DIR)/tmp && \
		apk add --no-cache apk-tools-static && \
		rm -rf /work/$(BUILD_DIR)/apkbundle && \
		mkdir -p /work/$(BUILD_DIR)/apkbundle/keys && \
		cp /sbin/apk.static /work/$(BUILD_DIR)/apkbundle/ && \
		cp /etc/apk/keys/*.rsa.pub /work/$(BUILD_DIR)/apkbundle/keys/ && \
		printf '%s\n' \
			'https://dl-cdn.alpinelinux.org/alpine/$(ALPINE_RELEASE)/main' \
			'https://dl-cdn.alpinelinux.org/alpine/$(ALPINE_RELEASE)/community' \
			> /work/$(BUILD_DIR)/apkbundle/repositories"

# 2. Package rootfs into initramfs.cpio.gz
initramfs:
	mkdir -p $(BUILD_DIR)
	(cd $(ROOTFS_DIR) && find . -print0 | cpio --null -ov --format=newc -R 0:0 | gzip -9 > ../initramfs.cpio.gz)

# 3. Compile Linux $(KERNEL_VERSION) from source. One shared source tree in the
# bishos-kernel docker volume; each arch builds out-of-tree (O=) into its own
# build dir, so x86_64 and arm64 stay incremental and never clobber each other.
kernel:
	mkdir -p $(BUILD_DIR)
	docker build -q -f Dockerfile.kernel -t bishos-kbuild .
	docker run --rm -v bishos-kernel:/kbuild -v "$$PWD":/src bishos-kbuild bash -c "\
		set -e && cd /kbuild && \
		if [ ! -f linux-$(KERNEL_VERSION).tar.xz ]; then \
			curl -sLO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$(KERNEL_VERSION).tar.xz; \
		fi && \
		echo '$(KERNEL_SHA256)  linux-$(KERNEL_VERSION).tar.xz' | sha256sum -c - && \
		if [ ! -d linux-$(KERNEL_VERSION) ]; then tar xf linux-$(KERNEL_VERSION).tar.xz; fi && \
		mkdir -p /kbuild/build-$(ARCH) && \
		cd linux-$(KERNEL_VERSION) && \
		make ARCH=$(ARCH) mrproper && \
		$(KMAKE) defconfig && \
		for s in $(KERNEL_CONFIG_ENABLE); do echo \"CONFIG_\$$s=y\"; done \
			>> /kbuild/build-$(ARCH)/.config && \
		$(KMAKE) olddefconfig && \
		$(KMAKE) -j\$$(nproc) $(KERNEL_IMAGE_TARGET) && \
		mkdir -p /src/$(BUILD_DIR) && cp /kbuild/build-$(ARCH)/$(KERNEL_ARTIFACT) /src/$(KERNEL)"

# 4. Create the persistent root filesystem: a raw ext4 disk image populated
# from the same rootfs staging tree, plus the apk package manager and sudo.
#
# sudo is installed here and not in the initramfs for the same reason apk is:
# busybox has no sudo applet, and the real one is dynamically linked, so it
# would drag the musl loader into an image that is otherwise entirely static.
# Note the chmod 4755 after the recursive chown -- chown drops the setuid bit,
# and sudo without setuid refuses to run at all.
#
# apk goes on the disk and NOT in the initramfs, for two reasons: the
# initramfs is loaded into RAM in full on every boot so every megabyte is a
# permanent cost, and a package manager without persistence is pointless --
# anything it installed would vanish at the next reboot. mke2fs -d fills the filesystem without
# mounting it, so this needs no loop device and no privileged container.
# The image is only created if missing -- rebuilding must not wipe the data
# that makes it worth having (use disk-reset for that).
disk: rootfs
	@if [ -f $(DISK) ]; then \
		echo "$(DISK) exists -- keeping its data (make ARCH=$(ARCH) disk-reset to recreate)"; \
	else \
		echo "Creating $(DISK) ($(DISK_SIZE) ext4)"; \
		docker run --rm --platform $(DOCKER_PLATFORM) -v "$$PWD":/work -w /work alpine:latest sh -c "\
			apk add --no-cache e2fsprogs e2fsprogs-extra apk-tools-static > /dev/null && \
			rm -rf /diskroot && cp -a /work/$(ROOTFS_DIR) /diskroot && \
			cp /work/$(ROOTFS_DIR)/init /diskroot/sbin/init && \
			mkdir -p /diskroot/etc/apk/keys /diskroot/var/lib/apk /diskroot/var/cache/apk && \
						cp /sbin/apk.static /diskroot/sbin/apk.static && \
			ln -sf apk.static /diskroot/sbin/apk && \
			cp /etc/apk/keys/*.rsa.pub /diskroot/etc/apk/keys/ && \
			printf '%s\n' \
				'https://dl-cdn.alpinelinux.org/alpine/$(ALPINE_RELEASE)/main' \
				'https://dl-cdn.alpinelinux.org/alpine/$(ALPINE_RELEASE)/community' \
				> /diskroot/etc/apk/repositories && \
			rm -rf /diskroot/etc/ssl && \
			/sbin/apk.static --root /diskroot --initdb add alpine-baselayout-data ca-certificates-bundle sudo openssh-server gcompat > /dev/null && \
			chown -R 0:0 /diskroot && \
			chmod 1777 /diskroot/tmp && chmod 700 /diskroot/root && \
			chmod 4755 /diskroot/usr/bin/sudo && \
			truncate -s $(DISK_SIZE) /work/$(DISK) && \
			mke2fs -t ext4 -F -L BISHOS -d /diskroot /work/$(DISK) > /dev/null && \
			echo done"; \
	fi

# Grow an existing persistent root to DISK_SIZE, keeping everything on it.
# This works in place because the image is a whole-disk ext4 filesystem with
# no partition table: there is no partition to move before resizing. The
# image is sparse, so a bigger DISK_SIZE costs host disk only as it fills.
disk-grow:
	@test -f $(DISK) || { echo "$(DISK) does not exist -- run: make ARCH=$(ARCH) disk"; exit 1; }
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$$PWD":/work -w /work alpine:latest sh -c "\
		apk add --no-cache e2fsprogs e2fsprogs-extra > /dev/null && \
		truncate -s $(DISK_SIZE) /work/$(DISK) && \
		e2fsck -fp /work/$(DISK) || true; \
		resize2fs /work/$(DISK)"
	@echo "$(DISK) grown to $(DISK_SIZE)"

# Refresh the system-owned files on an existing disk, keeping everything else.
#
# The disk target deliberately never touches an image that already exists --
# that is what protects your data. It is also what leaves a machine in daily
# use booting whatever init it was built with, long after the source moved on,
# and the failure is silent: the features are simply absent, which looks
# exactly like the features being broken.
#
# Replaced: /sbin/init and the BishOS-owned configuration under /etc.
# Never touched: accounts, passwords, home directories, installed packages.
# The sshd user is appended only if missing, since sshd will not start without
# it and an old disk predates it.
disk-update: rootfs
	@test -f $(DISK) || { echo "$(DISK) does not exist -- run: make ARCH=$(ARCH) disk"; exit 1; }
	docker run --rm --privileged --platform $(DOCKER_PLATFORM) -v "$$PWD":/work alpine:latest sh -c "\
		apk add --no-cache e2fsprogs > /dev/null && \
		mkdir -p /mnt/d && mount -o loop /work/$(DISK) /mnt/d && \
		cp /work/$(ROOTFS_DIR)/init /mnt/d/sbin/init && chmod 755 /mnt/d/sbin/init && \
		mkdir -p /mnt/d/etc/bishos /mnt/d/etc/sudoers.d /mnt/d/etc/ssh/sshd_config.d \
		         /mnt/d/var/log /mnt/d/var/empty && \
		cp /work/etc/bishos/* /mnt/d/etc/bishos/ && \
		chmod 644 /mnt/d/etc/bishos/services && \
		chmod 755 /mnt/d/etc/bishos/console /mnt/d/etc/bishos/sshd /mnt/d/etc/bishos/ntp-step && \
		cp /work/etc/sudoers.d/wheel /mnt/d/etc/sudoers.d/wheel && chmod 440 /mnt/d/etc/sudoers.d/wheel && \
		cp /work/etc/ssh/sshd_config.d/bishos.conf /mnt/d/etc/ssh/sshd_config.d/ && \
		grep -q '^sshd:' /mnt/d/etc/passwd || echo 'sshd:x:22:22:sshd:/dev/null:/sbin/nologin' >> /mnt/d/etc/passwd; \
		grep -q '^sshd:' /mnt/d/etc/group  || echo 'sshd:x:22:' >> /mnt/d/etc/group; \
		chown -R 0:0 /mnt/d/sbin/init /mnt/d/etc/bishos /mnt/d/etc/sudoers.d /mnt/d/etc/ssh/sshd_config.d && \
		chmod 4755 /mnt/d/usr/bin/sudo 2>/dev/null; \
		sync && umount /mnt/d && \
		e2fsck -fp /work/$(DISK) > /dev/null; true"
	@echo "$(DISK): init and /etc refreshed. Accounts, home directories and"
	@echo "installed packages were left alone. If sshd or sudo are missing,"
	@echo "the disk predates them -- boot it and: sudo apk add openssh-server sudo"

# Destroy and recreate the persistent root. Wipes everything on it.
disk-reset:
	rm -f $(DISK)
	$(MAKE) ARCH=$(ARCH) disk

# 5. Build a bootable ISO with GRUB -- identical layout and config on every
# architecture; only the kernel binary differs, and it is staged under the
# same name (/boot/vmlinuz). grub-mkrescue emits a UEFI boot path for both
# arches, and on x86_64 additionally a legacy-BIOS one in the same image.
# Finished ISOs go to output/, not build/. build/ is scratch -- staging trees,
# object files, disk images -- and make clean is entitled to delete all of it.
# The ISO is the one thing here anyone takes away, so it lives somewhere it
# can be found without knowing how the build is laid out, split by
# architecture so it is obvious which stick to write which file to.
OUTPUT_ROOT = output
OUTPUT_DIR = $(OUTPUT_ROOT)/$(ARCH)
ISO = $(OUTPUT_DIR)/bishos-$(ARCH)_v$(VERSION).iso

iso: all
	rm -rf $(BUILD_DIR)/iso
	mkdir -p $(BUILD_DIR)/iso/boot/grub $(OUTPUT_DIR)
	cp $(KERNEL) $(BUILD_DIR)/iso/boot/vmlinuz
	cp $(INITRAMFS) $(BUILD_DIR)/iso/boot/
	cp grub/grub.cfg $(BUILD_DIR)/iso/boot/grub/
	cp -r $(BUILD_DIR)/apkbundle $(BUILD_DIR)/iso/
	docker build --platform $(DOCKER_PLATFORM) -q -f Dockerfile.iso -t bishos-iso:$(ARCH) .
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$$PWD":/src bishos-iso:$(ARCH) \
		sh -c "cd /src && grub-mkrescue -o $(ISO) $(BUILD_DIR)/iso/"
	@echo "-> $(ISO)"

# Host port forwarded to the guest's sshd. QEMU's user-mode networking gives
# the guest outbound access but no inbound route, so without this the machine
# runs sshd that nothing can reach:  ssh -p $(SSH_PORT) bishal@localhost
SSH_PORT = 2222

# 6. Boot BishOS in QEMU: initramfs finds the virtio disk and switch_roots
# into it, so anything written to / survives a reboot.
run: all disk
	$(QEMU) \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=$(CONSOLE) quiet panic=1" \
		-drive file=$(DISK),if=virtio,format=raw \
		-nic user,model=$(NIC_MODEL),hostfwd=tcp::$(SSH_PORT)-:22 \
		-nographic \
		-m $(MEMORY)

# Used by CI to key the kernel source cache on the pinned version
print-kernel-version:
	@echo $(KERNEL_VERSION)

# Used by CI to name release artifacts
print-version:
	@echo $(VERSION)

# Scratch only. Finished ISOs in output/ are left alone -- use clean-output
# for those, so a rebuild never silently throws away something already burnt
# to a stick or handed to somebody.
clean:
	rm -rf build

# Every architecture, not just the one ARCH names -- otherwise "clean" would
# leave half the ISOs behind and look like it had failed.
clean-output:
	rm -rf $(OUTPUT_ROOT)
