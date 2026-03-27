# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin \
	-Iinclude -Ikernel -Ilib -Iarch/x86 -Imm -Ifs -Idrivers -Idrivers/base -Idrivers/input -Idrivers/tty -Idrivers/clocksource -Idrivers/video -Idrivers/console -Iinit
LDFLAGS = -m elf_i386 -T arch/x86/kernel/linker.ld -nostdlib

SOURCES_S = arch/x86/boot/boot.s arch/x86/kernel/interrupt.s
SOURCES_C = init/main.c kernel/syscall.c kernel/task.c kernel/fork.c kernel/sched.c kernel/exit.c kernel/exec.c kernel/proc_task.c \
	arch/x86/kernel/gdt.c arch/x86/kernel/idt.c arch/x86/kernel/isr.c arch/x86/kernel/setup.c \
	mm/mm.c mm/pmm.c mm/vmm.c mm/kheap.c mm/filemap.c lib/libc.c \
	fs/file.c fs/inode.c fs/dentry.c fs/namei.c fs/read_write.c fs/open.c fs/readdir.c fs/ioctl.c fs/namespace.c fs/ramfs/ramfs.c fs/ramfs/ramfs_driver.c fs/procfs/procfs.c fs/devfs/devfs.c \
	fs/sysfs/sysfs.c init/initramfs.c \
	drivers/base/device_model.c drivers/input/keyboard.c \
	drivers/clocksource/timer.c drivers/tty/tty.c drivers/tty/serial.c \
	drivers/video/vga.c drivers/console/console.c drivers/console/console_driver.c
OBJECTS = $(SOURCES_S:.s=.o) $(SOURCES_C:.c=.o)

OUT_DIR = out
KERNEL = $(OUT_DIR)/myos.bin
ISO = $(OUT_DIR)/myos.iso
INITRAMFS = $(OUT_DIR)/initramfs.cpio

USH_ELF = $(OUT_DIR)/shell.elf
USH_OBJS = usr/crt0.o usr/ulib.o usr/shell.o
INIT_ELF = $(OUT_DIR)/init.elf
INIT_OBJS = usr/crt0.o usr/ulib.o usr/init.o
UNIT_TEST_ELF = $(OUT_DIR)/smoke.elf
UNIT_TEST_OBJS = usr/crt0.o usr/ulib.o usr/smoke.o
USER_ELFS = $(USH_ELF) $(INIT_ELF) $(UNIT_TEST_ELF)

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
	cp $(INIT_ELF) rootfs/sbin/init
	cp $(USH_ELF) rootfs/sbin/sh
	# Copy other tools to /bin (dropping .elf extension for aesthetics)
	cp $(UNIT_TEST_ELF) rootfs/bin/smoke
	cd rootfs && find . | cpio -o -H newc > ../$(INITRAMFS)
	rm -rf rootfs

$(USH_ELF): $(USH_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(USH_OBJS)

$(INIT_ELF): $(INIT_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(INIT_OBJS)

$(UNIT_TEST_ELF): $(UNIT_TEST_OBJS) usr/ulinker.ld
	$(LD) -m elf_i386 -T usr/ulinker.ld -o $@ $(UNIT_TEST_OBJS)

run: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -serial stdio

debug: $(KERNEL) $(INITRAMFS)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRAMFS) -m 512M -s -S -serial stdio

clean:
	rm -f $(OBJECTS) $(USH_OBJS) $(INIT_OBJS) $(UNIT_TEST_OBJS)
	rm -rf $(OUT_DIR) isodir rootfs

.PHONY: all iso run run-iso clean
