BUILD_DIR = build
ROOTFS_DIR = $(BUILD_DIR)/rootfs
INITRAMFS = $(BUILD_DIR)/initramfs.cpio.gz
KERNEL = $(BUILD_DIR)/bzImage

.PHONY: all clean rootfs initramfs kernel run

all: rootfs initramfs

# 1. Compile C init (PID 1) and install BusyBox + symlinks + user profiles + ONLY standalone e1000 network driver
rootfs:
	mkdir -p $(BUILD_DIR)
	docker run --rm --platform linux/amd64 -v "$$PWD":/work -w /work alpine:latest sh -c "\
		apk add --no-cache gcc musl-dev busybox-static && \
		rm -rf /work/$(ROOTFS_DIR) && \
		mkdir -p /work/$(ROOTFS_DIR)/bin /work/$(ROOTFS_DIR)/sbin \
		         /work/$(ROOTFS_DIR)/proc /work/$(ROOTFS_DIR)/sys \
		         /work/$(ROOTFS_DIR)/dev /work/$(ROOTFS_DIR)/root \
		         /work/$(ROOTFS_DIR)/home/bishal /work/$(ROOTFS_DIR)/tmp \
		         /work/$(ROOTFS_DIR)/etc /work/$(ROOTFS_DIR)/usr/share/udhcpc \
		         /work/$(ROOTFS_DIR)/lib/modules && \
		gcc -static -O2 /work/src/init.c -o /work/$(ROOTFS_DIR)/init && \
		cp /bin/busybox.static /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod +x /work/$(ROOTFS_DIR)/bin/busybox && \
		cd /work/$(ROOTFS_DIR)/bin && \
		for app in \$$(./busybox --list); do ln -sf busybox \$$app; done && \
		cd /work/$(ROOTFS_DIR)/sbin && \
		for app in \$$(../bin/busybox --list); do ln -sf /bin/busybox \$$app; done && \
		cp -r /work/etc/* /work/$(ROOTFS_DIR)/etc/ && \
		cp /work/etc/udhcpc/default.script /work/$(ROOTFS_DIR)/usr/share/udhcpc/default.script && \
		mkdir -p /tmp/k && cd /tmp/k && \
		apk fetch --no-cache linux-lts && \
		tar -xzf linux-lts-*.apk && \
		cp boot/vmlinuz-lts /work/$(KERNEL) && \
		E1000=\$$(find lib/modules -name 'e1000.ko.gz' | head -n 1) && \
		gzip -dc \"\$$E1000\" > /work/$(ROOTFS_DIR)/lib/modules/e1000.ko && \
		find /work/$(ROOTFS_DIR) -type d -exec chmod 755 {} + && \
		find /work/$(ROOTFS_DIR)/etc -type f -exec chmod 644 {} + && \
		chmod 755 /work/$(ROOTFS_DIR)/etc/udhcpc/default.script /work/$(ROOTFS_DIR)/usr/share/udhcpc/default.script && \
		chmod 755 /work/$(ROOTFS_DIR)/init /work/$(ROOTFS_DIR)/bin/busybox && \
		chmod 644 /work/$(ROOTFS_DIR)/lib/modules/e1000.ko && \
		chmod 700 /work/$(ROOTFS_DIR)/root && \
		chmod 1777 /work/$(ROOTFS_DIR)/tmp"

# 2. Package rootfs into initramfs.cpio.gz
initramfs:
	mkdir -p $(BUILD_DIR)
	(cd $(ROOTFS_DIR) && find . -print0 | cpio --null -ov --format=newc -R 0:0 | gzip -9 > ../initramfs.cpio.gz)

# 3. Kernel alias (already built by rootfs)
kernel:
	@test -f $(KERNEL) || $(MAKE) rootfs

# 4. Boot BishOS in QEMU with User-mode Virtual Network Card
run:
	qemu-system-x86_64 \
		-kernel $(KERNEL) \
		-initrd $(INITRAMFS) \
		-append "console=ttyS0 quiet panic=1" \
		-nic user,model=e1000 \
		-nographic \
		-m 256M

clean:
	rm -rf $(BUILD_DIR)
