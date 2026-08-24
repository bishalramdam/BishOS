BUILD_DIR = build
ROOTFS_DIR = $(BUILD_DIR)/rootfs
INITRAMFS = $(BUILD_DIR)/initramfs.cpio.gz
KERNEL = $(BUILD_DIR)/bzImage

# Kernel source pin -- bump both together (hash from cdn.kernel.org sha256sums.asc)
KERNEL_VERSION = 6.18.46
KERNEL_SHA256 = f5d44b93808b02cc2969c5404ba081d97523719c9fd2ba2de6db318b4141cca0

.PHONY: all clean rootfs initramfs kernel run

all: kernel rootfs initramfs

# 1. Compile C init (PID 1) and install BusyBox + symlinks + user profiles
rootfs:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform linux/amd64 -v "$$PWD":/work -w /work alpine:latest sh -c "\
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

# 3. Compile Linux $(KERNEL_VERSION) from source (cross-compiled in a native arm64
# container; the tree lives in the bishos-kernel docker volume so rebuilds are incremental)
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
		cd linux-$(KERNEL_VERSION) && \
		make ARCH=x86_64 CROSS_COMPILE=x86_64-linux-gnu- x86_64_defconfig && \
		make ARCH=x86_64 CROSS_COMPILE=x86_64-linux-gnu- -j\$$(nproc) && \
		mkdir -p /src/$(BUILD_DIR) && cp arch/x86/boot/bzImage /src/$(KERNEL)"

# 4. Boot BishOS in QEMU with User-mode Virtual Network Card
run: all
	qemu-system-x86_64 \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=ttyS0 quiet panic=1" \
		-nic user,model=e1000 \
		-nographic \
		-m 256M

clean:
	rm -rf $(BUILD_DIR)
