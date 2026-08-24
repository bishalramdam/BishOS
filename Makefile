# Target architecture: x86_64 (default) or arm64.
#   make ARCH=arm64 run   -> native on Apple Silicon, hardware-accelerated via HVF
#   make run              -> x86_64, software-emulated (TCG)
ARCH ?= x86_64

BUILD_DIR = build/$(ARCH)
ROOTFS_DIR = $(BUILD_DIR)/rootfs
INITRAMFS = $(BUILD_DIR)/initramfs.cpio.gz

# Kernel source pin -- bump both together (hash from cdn.kernel.org sha256sums.asc)
KERNEL_VERSION = 6.18.46
KERNEL_SHA256 = f5d44b93808b02cc2969c5404ba081d97523719c9fd2ba2de6db318b4141cca0

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

.PHONY: all clean rootfs initramfs kernel run iso print-kernel-version

all: kernel rootfs initramfs

# 1. Compile C init (PID 1) and install BusyBox + symlinks + user profiles
rootfs:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform $(DOCKER_PLATFORM) -v "$$PWD":/work -w /work alpine:latest sh -c "\
		apk add --no-cache gcc musl-dev busybox-static && \
		rm -rf /work/$(ROOTFS_DIR) && \
		mkdir -p /work/$(ROOTFS_DIR)/bin /work/$(ROOTFS_DIR)/sbin \
		         /work/$(ROOTFS_DIR)/proc /work/$(ROOTFS_DIR)/sys \
		         /work/$(ROOTFS_DIR)/dev /work/$(ROOTFS_DIR)/root \
		         /work/$(ROOTFS_DIR)/home/bishal /work/$(ROOTFS_DIR)/tmp \
		         /work/$(ROOTFS_DIR)/etc /work/$(ROOTFS_DIR)/usr/share/udhcpc && \
		gcc -static -O2 /work/src/init.c -o /work/$(ROOTFS_DIR)/init && \
		cp /bin/busybox.static /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod +x /work/$(ROOTFS_DIR)/bin/busybox && \
		cd /work/$(ROOTFS_DIR)/bin && \
		for app in \$$(./busybox --list); do ln -sf busybox \$$app; done && \
		cd /work/$(ROOTFS_DIR)/sbin && \
		for app in \$$(../bin/busybox --list); do ln -sf /bin/busybox \$$app; done && \
		cp -r /work/etc/* /work/$(ROOTFS_DIR)/etc/ && \
		cp /work/etc/udhcpc/default.script /work/$(ROOTFS_DIR)/usr/share/udhcpc/default.script && \
		find /work/$(ROOTFS_DIR) -type d -exec chmod 755 {} + && \
		find /work/$(ROOTFS_DIR)/etc -type f -exec chmod 644 {} + && \
		chmod 755 /work/$(ROOTFS_DIR)/etc/udhcpc/default.script /work/$(ROOTFS_DIR)/usr/share/udhcpc/default.script && \
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
		$(KMAKE) -j\$$(nproc) $(KERNEL_IMAGE_TARGET) && \
		mkdir -p /src/$(BUILD_DIR) && cp /kbuild/build-$(ARCH)/$(KERNEL_ARTIFACT) /src/$(KERNEL)"

# 4. Build a bootable ISO: arm64 -> UEFI (GRUB, for VMware Fusion / EDK2),
# x86_64 -> legacy BIOS (isolinux, El Torito + hybrid MBR so the same file
# boots from CD and from a dd'd USB stick).
ISO = $(BUILD_DIR)/bishos-$(ARCH).iso

iso: all
	rm -rf $(BUILD_DIR)/iso
ifeq ($(ARCH),arm64)
	mkdir -p $(BUILD_DIR)/iso/boot/grub
	cp $(KERNEL) $(INITRAMFS) $(BUILD_DIR)/iso/boot/
	cp grub/grub.cfg $(BUILD_DIR)/iso/boot/grub/
	docker run --rm -v "$$PWD":/src bishos-kbuild bash -c \
		"cd /src/$(BUILD_DIR) && grub-mkrescue -o bishos-arm64.iso iso/"
else
	mkdir -p $(BUILD_DIR)/iso/boot $(BUILD_DIR)/iso/isolinux
	cp $(KERNEL) $(INITRAMFS) $(BUILD_DIR)/iso/boot/
	cp isolinux/isolinux.cfg $(BUILD_DIR)/iso/isolinux/
	docker run --rm -v "$$PWD":/src bishos-kbuild bash -c "\
		cp /usr/lib/ISOLINUX/isolinux.bin \
		   /usr/lib/syslinux/modules/bios/ldlinux.c32 \
		   /src/$(BUILD_DIR)/iso/isolinux/ && \
		cd /src/$(BUILD_DIR) && \
		xorriso -as mkisofs -o bishos-x86_64.iso -V BISHOS \
			-b isolinux/isolinux.bin -c isolinux/boot.cat \
			-no-emul-boot -boot-load-size 4 -boot-info-table \
			-isohybrid-mbr /usr/lib/ISOLINUX/isohdpfx.bin \
			iso/"
endif

# 5. Boot BishOS in QEMU with User-mode Virtual Network Card
run: all
	$(QEMU) \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=$(CONSOLE) quiet panic=1" \
		-nic user,model=$(NIC_MODEL) \
		-nographic \
		-m 256M

# Used by CI to key the kernel source cache on the pinned version
print-kernel-version:
	@echo $(KERNEL_VERSION)

clean:
	rm -rf build
