# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin
CFLAGS += -MMD -MP
CFLAGS += -Iinclude -Ikernel -Iinit -Ilib -Iarch/x86 -Imm -Ifs -Idrivers -Idrivers/base -Idrivers/pci -Idrivers/pci/pcie -Idrivers/nvme -Idrivers/input -Idrivers/tty -Idrivers/tty/serial -Idrivers/clocksource -Idrivers/scsi
LDFLAGS = -m elf_i386 -T arch/x86/kernel/linker.ld -nostdlib

SOURCES_S = arch/x86/boot/boot.s arch/x86/kernel/interrupt.s
SOURCES_C = init/main.c init/version.c kernel/syscall.c kernel/fork.c kernel/pid.c kernel/cred.c kernel/sched.c kernel/exit.c kernel/ksysfs.c kernel/panic.c kernel/printk.c kernel/params.c kernel/time.c kernel/clockevents.c kernel/signal.c kernel/wait.c kernel/kthread.c
SOURCES_C += arch/x86/kernel/gdt.c arch/x86/kernel/idt.c arch/x86/kernel/isr.c arch/x86/kernel/setup.c
SOURCES_C += arch/x86/kernel/irq.c arch/x86/kernel/i8259.c arch/x86/kernel/apic.c arch/x86/kernel/io_apic.c
SOURCES_C += mm/bootmem.c mm/mmzone.c mm/mmap.c mm/page_alloc.c mm/vmscan.c mm/memory.c mm/vmalloc.c mm/slab.c mm/filemap.c mm/rmap.c mm/swap.c lib/libc.c lib/vsprintf.c lib/kref.c lib/kobject.c lib/klist.c lib/bitmap.c lib/rbtree.c lib/idr.c lib/parser.c
SOURCES_C += fs/file.c fs/fdtable.c fs/exec.c fs/inode.c fs/dentry.c fs/namei.c fs/read_write.c fs/open.c fs/readdir.c fs/ioctl.c fs/namespace.c fs/ramfs/ramfs.c fs/procfs/procfs.c fs/procfs/base.c fs/procfs/array.c fs/procfs/task_mmu.c fs/block_dev.c fs/chrdev.c fs/minixfs/minixfs.c
SOURCES_C += fs/buffer.c
SOURCES_C += block/blk-core.c
SOURCES_C += block/blkdev.c
SOURCES_C += fs/sysfs/sysfs.c init/initramfs.c
SOURCES_C += drivers/base/core.c drivers/base/virtual.c drivers/base/uevent.c drivers/base/bus.c drivers/base/class.c drivers/base/driver.c drivers/base/platform.c drivers/base/devtmpfs.c drivers/base/init.c drivers/pci/pci.c drivers/pci/pcie/pcie.c drivers/nvme/nvme-core.c drivers/nvme/nvme.c drivers/input/serio/serio.c drivers/input/serio/i8042.c drivers/input/keyboard/atkbd.c drivers/block/ramdisk.c
SOURCES_C += drivers/clocksource/timer.c drivers/tty/tty_io.c drivers/tty/n_tty.c drivers/tty/serial/serial_core.c drivers/tty/serial/8250.c
# printk/console core lives in kernel/printk.c; no video console backends.
SOURCES_C += drivers/virtio/virtio.c drivers/virtio/virtio_pci.c drivers/virtio/virtqueue.c
SOURCES_C += drivers/scsi/scsi_sysfs.c drivers/scsi/hosts.c drivers/scsi/scsi.c drivers/scsi/sd.c drivers/scsi/virtio_scsi.c
OBJECTS = $(SOURCES_S:.s=.o) $(SOURCES_C:.c=.o)
DEPS = $(OBJECTS:.o=.d) $(USH_OBJS:.o=.d) $(INIT_OBJS:.o=.d) $(SMOKE_OBJS:.o=.d)

OUT_DIR = out
KERNEL = $(OUT_DIR)/myos.bin
ISO = $(OUT_DIR)/myos.iso
INITRAMFS = $(OUT_DIR)/initramfs.cpio
SCSI_IMG = scsi.img
NVME_IMG0 = nvme0.img
NVME_IMG1 = nvme1.img

USH_ELF = $(OUT_DIR)/shell.elf
USH_OBJS = usr/crt0.o usr/ulib.o usr/shell.o
INIT_ELF = $(OUT_DIR)/init.elf
INIT_OBJS = usr/crt0.o usr/ulib.o usr/init.o
SMOKE_ELF = $(OUT_DIR)/smoke.elf
SMOKE_OBJS = usr/crt0.o usr/ulib.o usr/smoke.o
USER_ELFS = $(USH_ELF) $(INIT_ELF) $(SMOKE_ELF)

all: $(OUT_DIR) $(KERNEL) $(INITRAMFS)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(KERNEL): $(OBJECTS) arch/x86/kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

-include $(DEPS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) --32 $< -o $@

# Create a bootable ISO image using GRUB
iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/$(KERNEL)
	echo 'menuentry "My Minimal OS" {' > isodir/boot/grub/grub.cfg
	echo '	multiboot /boot/$(KERNEL)' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) isodir
	@echo "\nISO image created successfully: $(ISO)"
	@echo "To run the ISO with QEMU, execute manually:"
	@echo "qemu-system-i386 -cdrom $(ISO) -curses"

$(INITRAMFS): $(USER_ELFS)
	mkdir -p rootfs/sbin
	mkdir -p rootfs/bin
	# Copy init and shell to /sbin
	cp $(INIT_ELF) rootfs/sbin/init && :
	cp $(USH_ELF) rootfs/sbin/sh
	# Copy other tools to /bin (dropping .elf extension for aesthetics)
	cp $(SMOKE_ELF) rootfs/bin/smoke
	ln -snf ../sbin/sh rootfs/bin/sh-rel
	ln -snf smoke rootfs/bin/smoke-link
	ln -snf loop-b rootfs/bin/loop-a
	ln -snf loop-a rootfs/bin/loop-b
	cd rootfs && find . | cpio -o -H newc > ../$(INITRAMFS)
	rm -rf rootfs

$(USH_ELF): $(USH_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(USH_OBJS)

$(INIT_ELF): $(INIT_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(INIT_OBJS)

$(SMOKE_ELF): $(SMOKE_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(SMOKE_OBJS)

VIRTIO_SCSI_ARGS = -drive file=$(SCSI_IMG),format=raw,if=none,id=scsidisk0 -device virtio-scsi-pci,id=scsi0,disable-modern=on -device scsi-hd,drive=scsidisk0,bus=scsi0.0
NVME_ARGS = -drive file=$(NVME_IMG0),format=raw,if=none,id=nvme0 -device nvme,drive=nvme0,serial=NVME0001 -drive file=$(NVME_IMG1),format=raw,if=none,id=nvme1 -device nvme,drive=nvme1,serial=NVME0002

$(SCSI_IMG):
	truncate -s 16M $(SCSI_IMG)

$(NVME_IMG0):
	truncate -s 16M $(NVME_IMG0)

$(NVME_IMG1):
	truncate -s 16M $(NVME_IMG1)

run: $(KERNEL) $(INITRAMFS) $(SCSI_IMG)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -serial stdio $(VIRTIO_SCSI_ARGS)

SMOKE_TIMEOUT ?= 30
SMOKE_INPUT_DELAY ?= 5

smoke: smoke-512

smoke-512: $(KERNEL) $(INITRAMFS) $(SCSI_IMG) $(NVME_IMG0) $(NVME_IMG1)
	sh -c 'tmp=$$(mktemp); scsi=$$(mktemp); nv0=$$(mktemp); nv1=$$(mktemp); cp $(SCSI_IMG) $$scsi; cp $(NVME_IMG0) $$nv0; cp $(NVME_IMG1) $$nv1; timeout $(SMOKE_TIMEOUT)s sh -c "{ sleep $(SMOKE_INPUT_DELAY); printf \"run /bin/smoke\\nexit\\n\"; } | qemu-system-i386 -machine q35 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -display none -monitor none -serial stdio -drive file=$$scsi,format=raw,if=none,id=scsidisk0 -device virtio-scsi-pci,id=scsi0,disable-modern=on -device scsi-hd,drive=scsidisk0,bus=scsi0.0 -drive file=$$nv0,format=raw,if=none,id=nvme0 -device nvme,drive=nvme0,serial=NVME0001 -drive file=$$nv1,format=raw,if=none,id=nvme1 -device nvme,drive=nvme1,serial=NVME0002" >$$tmp 2>&1; cat $$tmp; rm -f $$scsi $$nv0 $$nv1; grep -q "All tests completed (OK)." $$tmp'

smoke-128: $(KERNEL) $(INITRAMFS) $(SCSI_IMG) $(NVME_IMG0) $(NVME_IMG1)
	sh -c 'tmp=$$(mktemp); scsi=$$(mktemp); nv0=$$(mktemp); nv1=$$(mktemp); cp $(SCSI_IMG) $$scsi; cp $(NVME_IMG0) $$nv0; cp $(NVME_IMG1) $$nv1; timeout $(SMOKE_TIMEOUT)s sh -c "{ sleep $(SMOKE_INPUT_DELAY); printf \"run /bin/smoke\\nexit\\n\"; } | qemu-system-i386 -machine q35 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 128M -display none -monitor none -serial stdio -drive file=$$scsi,format=raw,if=none,id=scsidisk0 -device virtio-scsi-pci,id=scsi0,disable-modern=on -device scsi-hd,drive=scsidisk0,bus=scsi0.0 -drive file=$$nv0,format=raw,if=none,id=nvme0 -device nvme,drive=nvme0,serial=NVME0001 -drive file=$$nv1,format=raw,if=none,id=nvme1 -device nvme,drive=nvme1,serial=NVME0002" >$$tmp 2>&1; cat $$tmp; rm -f $$scsi $$nv0 $$nv1; grep -q "All tests completed (OK)." $$tmp'

check-vocab:
	sh scripts/check-vocab.sh

run-nvme: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -machine q35 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -serial stdio -drive file=nvme.img,format=raw,if=none,id=nvme0 -device nvme,drive=nvme0,serial=NVME0001

run-virtio-scsi: $(KERNEL) $(INITRAMFS) $(SCSI_IMG)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -serial stdio $(VIRTIO_SCSI_ARGS)

debug: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -s -S -serial stdio

clean:
	rm -f $(OBJECTS) $(USH_OBJS) $(INIT_OBJS) $(SMOKE_OBJS) $(DEPS)
	rm -rf $(OUT_DIR) isodir rootfs

.PHONY: all iso run run-iso smoke smoke-512 smoke-128 check-vocab clean run-virtio-scsi
