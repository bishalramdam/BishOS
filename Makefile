# Target architecture: x86_64 (default) or arm64.
#   make ARCH=arm64 run   -> native on Apple Silicon, hardware-accelerated via HVF
#   make run              -> x86_64, software-emulated (TCG)
ARCH ?= x86_64

# Single source of truth for the version: compiled into init's banner and
# used in ISO filenames, so a release artifact always names what it is.
VERSION = 0.7.0

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

# Options defconfig does not set that we need. x86_64's defconfig ships only
# the legacy VGA text console, which does not exist under UEFI -- the firmware
# hands over a linear framebuffer -- so without a framebuffer console the
# screen stays black after GRUB on real hardware. arm64 already has these;
# setting them for both keeps the two kernels honest.
KERNEL_CONFIG_ENABLE = FB FB_EFI FB_VESA FRAMEBUFFER_CONSOLE \
                       FRAMEBUFFER_CONSOLE_DETECT_PRIMARY

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

.PHONY: all clean rootfs initramfs kernel run iso disk disk-reset disk-grow print-kernel-version print-version

all: kernel rootfs initramfs

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
		         /work/$(ROOTFS_DIR)/etc/ssl/certs /work/$(ROOTFS_DIR)/var/log && \
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
		chmod 755 /work/$(ROOTFS_DIR)/init /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod 700 /work/$(ROOTFS_DIR)/root && \
		chmod 1777 /work/$(ROOTFS_DIR)/tmp"

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
		./scripts/config --file /kbuild/build-$(ARCH)/.config \
			$(foreach c,$(KERNEL_CONFIG_ENABLE),-e $(c)) && \
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
			/sbin/apk.static --root /diskroot --initdb add alpine-baselayout-data ca-certificates-bundle sudo > /dev/null && \
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

# Destroy and recreate the persistent root. Wipes everything on it.
disk-reset:
	rm -f $(DISK)
	$(MAKE) ARCH=$(ARCH) disk

# 5. Build a bootable ISO with GRUB -- identical layout and config on every
# architecture; only the kernel binary differs, and it is staged under the
# same name (/boot/vmlinuz). grub-mkrescue emits a UEFI boot path for both
# arches, and on x86_64 additionally a legacy-BIOS one in the same image.
ISO = $(BUILD_DIR)/bishos-$(ARCH)_v$(VERSION).iso

iso: all
	rm -rf $(BUILD_DIR)/iso
	mkdir -p $(BUILD_DIR)/iso/boot/grub
	cp $(KERNEL) $(BUILD_DIR)/iso/boot/vmlinuz
	cp $(INITRAMFS) $(BUILD_DIR)/iso/boot/
	cp grub/grub.cfg $(BUILD_DIR)/iso/boot/grub/
	docker build --platform $(DOCKER_PLATFORM) -q -f Dockerfile.iso -t bishos-iso:$(ARCH) .
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$$PWD":/src bishos-iso:$(ARCH) \
		sh -c "cd /src/$(BUILD_DIR) && grub-mkrescue -o bishos-$(ARCH)_v$(VERSION).iso iso/"

# 6. Boot BishOS in QEMU: initramfs finds the virtio disk and switch_roots
# into it, so anything written to / survives a reboot.
run: all disk
	$(QEMU) \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=$(CONSOLE) quiet panic=1" \
		-drive file=$(DISK),if=virtio,format=raw \
		-nic user,model=$(NIC_MODEL) \
		-nographic \
		-m $(MEMORY)

# Used by CI to key the kernel source cache on the pinned version
print-kernel-version:
	@echo $(KERNEL_VERSION)

# Used by CI to name release artifacts
print-version:
	@echo $(VERSION)

clean:
	rm -rf build
