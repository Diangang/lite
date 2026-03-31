# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin
CFLAGS += -Iinclude -Ikernel -Iinit -Ilib -Iarch/x86 -Imm -Ifs -Idrivers -Idrivers/base -Idrivers/pci -Idrivers/pci/pcie -Idrivers/nvme -Idrivers/input -Idrivers/tty -Idrivers/tty/serial -Idrivers/clocksource -Idrivers/video -Idrivers/video/console
LDFLAGS = -m elf_i386 -T arch/x86/kernel/linker.ld -nostdlib

SOURCES_S = arch/x86/boot/boot.s arch/x86/kernel/interrupt.s
SOURCES_C = init/main.c init/version.c kernel/syscall.c kernel/fork.c kernel/pid.c kernel/cred.c kernel/sched.c kernel/exit.c kernel/ksysfs.c kernel/panic.c kernel/printk.c kernel/params.c kernel/time.c kernel/signal.c kernel/wait.c
SOURCES_C += arch/x86/kernel/gdt.c arch/x86/kernel/idt.c arch/x86/kernel/isr.c arch/x86/kernel/setup.c
SOURCES_C += arch/x86/kernel/irq.c
SOURCES_C += mm/mm.c mm/bootmem.c mm/mmzone.c mm/mmap.c mm/page_alloc.c mm/vmscan.c mm/memory.c mm/vmalloc.c mm/slab.c mm/filemap.c mm/rmap.c mm/swap.c lib/libc.c lib/kref.c lib/kobject.c lib/bitmap.c lib/rbtree.c lib/idr.c lib/parser.c
SOURCES_C += fs/file.c fs/fdtable.c fs/exec.c fs/inode.c fs/dentry.c fs/namei.c fs/read_write.c fs/open.c fs/readdir.c fs/ioctl.c fs/namespace.c fs/ramfs/ramfs.c fs/procfs/procfs.c fs/procfs/base.c fs/procfs/array.c fs/procfs/task_mmu.c fs/devtmpfs/devtmpfs.c
SOURCES_C += fs/sysfs/sysfs.c init/initramfs.c
SOURCES_C += drivers/base/core.c drivers/base/bus.c drivers/base/driver.c drivers/base/init.c drivers/pci/pci.c drivers/pci/pcie/pcie.c drivers/nvme/nvme.c drivers/input/keyboard.c
SOURCES_C += drivers/clocksource/timer.c drivers/tty/tty.c drivers/tty/serial/serial.c
SOURCES_C += drivers/video/console/vga.c drivers/video/console/console.c drivers/video/console/console_driver.c
OBJECTS = $(SOURCES_S:.s=.o) $(SOURCES_C:.c=.o)

OUT_DIR = out
KERNEL = $(OUT_DIR)/myos.bin
ISO = $(OUT_DIR)/myos.iso
INITRAMFS = $(OUT_DIR)/initramfs.cpio

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
	cd rootfs && find . | cpio -o -H newc > ../$(INITRAMFS)
	rm -rf rootfs

$(USH_ELF): $(USH_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(USH_OBJS)

$(INIT_ELF): $(INIT_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(INIT_OBJS)

$(SMOKE_ELF): $(SMOKE_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(SMOKE_OBJS)

run: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -serial stdio

SMOKE_TIMEOUT ?= 30

smoke: smoke-512

smoke-512: $(KERNEL) $(INITRAMFS)
	sh -c 'tmp=$$(mktemp); timeout $(SMOKE_TIMEOUT)s sh -c "{ sleep 2; printf \"run /bin/smoke\\nexit\\n\"; } | qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -display none -monitor none -serial stdio" >$$tmp 2>&1; cat $$tmp; grep -q "All tests completed (OK)." $$tmp'

smoke-128: $(KERNEL) $(INITRAMFS)
	sh -c 'tmp=$$(mktemp); timeout $(SMOKE_TIMEOUT)s sh -c "{ sleep 2; printf \"run /bin/smoke\\nexit\\n\"; } | qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 128M -display none -monitor none -serial stdio" >$$tmp 2>&1; cat $$tmp; grep -q "All tests completed (OK)." $$tmp'

run-nvme: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -machine q35 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -serial stdio -drive file=nvme.img,format=raw,if=none,id=nvme0 -device nvme,drive=nvme0,serial=NVME0001

debug: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -s -S -serial stdio

clean:
	rm -f $(OBJECTS) $(USH_OBJS) $(INIT_OBJS) $(SMOKE_OBJS)
	rm -rf $(OUT_DIR) isodir rootfs

.PHONY: all iso run run-iso smoke smoke-512 smoke-128 clean
