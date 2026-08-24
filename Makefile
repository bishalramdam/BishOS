BUILD_DIR = build
ROOTFS_DIR = $(BUILD_DIR)/rootfs
INITRAMFS = $(BUILD_DIR)/initramfs.cpio.gz
KERNEL = $(BUILD_DIR)/bzImage

.PHONY: all clean rootfs initramfs kernel run

all: rootfs initramfs kernel

# 1. Compile C init (PID 1) and install BusyBox + symlinks
rootfs:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform linux/amd64 -v "$$PWD":/work -w /work alpine:latest sh -c "\
		apk add --no-cache gcc musl-dev busybox-static && \
		rm -rf /work/$(ROOTFS_DIR) && \
		mkdir -p /work/$(ROOTFS_DIR)/bin /work/$(ROOTFS_DIR)/sbin \
		         /work/$(ROOTFS_DIR)/proc /work/$(ROOTFS_DIR)/sys \
		         /work/$(ROOTFS_DIR)/dev /work/$(ROOTFS_DIR)/root \
		         /work/$(ROOTFS_DIR)/tmp && \
		gcc -static -O2 /work/src/init.c -o /work/$(ROOTFS_DIR)/init && \
		cp /bin/busybox.static /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod +x /work/$(ROOTFS_DIR)/bin/busybox && \
		cd /work/$(ROOTFS_DIR)/bin && \
		for app in \$$(./busybox --list); do ln -sf busybox \$$app; done && \
		cd /work/$(ROOTFS_DIR)/sbin && \
		for app in \$$(../bin/busybox --list); do ln -sf /bin/busybox \$$app; done"

# 2. Package rootfs into initramfs.cpio.gz
initramfs:
	mkdir -p $(BUILD_DIR)
	(cd $(ROOTFS_DIR) && find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs.cpio.gz)

# 3. Fetch Linux LTS kernel image (if not already downloaded)
kernel:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform linux/amd64 -v "$$PWD":/work -w /work alpine:latest sh -c "\
		if [ ! -f /work/$(KERNEL) ]; then \
			mkdir -p /tmp/k && cd /tmp/k && \
			apk fetch --no-cache linux-lts && \
			tar -xzf linux-lts-*.apk && \
			cp boot/vmlinuz-lts /work/$(KERNEL); \
		fi"

# 4. Boot BishOS in QEMU
run:
	qemu-system-x86_64 \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=ttyS0 quiet panic=1" \
		-nographic \
		-m 256M

clean:
	rm -rf $(BUILD_DIR)
