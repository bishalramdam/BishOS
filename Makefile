CC = gcc
ARCH ?= x86_64

BUILD_DIR = build
ROOTFS_DIR = $(BUILD_DIR)/rootfs
INITRAMFS = $(BUILD_DIR)/initramfs.cpio.gz
KERNEL = $(BUILD_DIR)/bzImage

.PHONY: all clean init initramfs kernel run

all: init initramfs kernel

# Compile static init binary using Docker (Linux musl-libc)
init:
	mkdir -p $(ROOTFS_DIR)
	docker run --rm --platform linux/amd64 -v "$$PWD":/work -w /work alpine:latest \
		sh -c "apk add --no-cache gcc musl-dev && gcc -static -O2 src/init.c -o build/rootfs/init"

# Package root filesystem into cpio.gz archive
initramfs:
	mkdir -p $(BUILD_DIR)
	(cd $(ROOTFS_DIR) && find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initramfs.cpio.gz)

# Fetch minimal Linux LTS kernel image
kernel:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform linux/amd64 -v "$$PWD":/work -w /work alpine:latest \
		sh -c "mkdir -p /tmp/k && cd /tmp/k && apk fetch --no-cache linux-lts && tar -xzf linux-lts-*.apk && cp boot/vmlinuz-lts /work/build/bzImage"

# Boot BishOS in QEMU
run:
	qemu-system-x86_64 \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=ttyS0 quiet panic=1" \
		-nographic \
		-m 256M

clean:
	rm -rf $(BUILD_DIR)
