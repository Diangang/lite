# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin \
	-Iinclude -Ikernel -Ilib -Iarch/x86 -Imm -Ifs -Idrivers -Idrivers/base -Idrivers/input -Idrivers/tty -Idrivers/clocksource -Idrivers/video -Idrivers/console -Iinit
LDFLAGS = -m elf_i386 -T arch/x86/kernel/linker.ld -nostdlib

SOURCES_S = arch/x86/boot/boot.s arch/x86/kernel/interrupt.s
SOURCES_C = init/main.c kernel/syscall.c kernel/task.c \
	arch/x86/kernel/gdt.c arch/x86/kernel/idt.c arch/x86/kernel/isr.c arch/x86/kernel/setup.c \
	mm/mm.c mm/pmm.c mm/vmm.c mm/kheap.c mm/filemap.c lib/libc.c \
        fs/file.c fs/inode.c fs/dentry.c fs/namei.c fs/read_write.c fs/open.c fs/readdir.c fs/ioctl.c fs/namespace.c fs/ramfs/ramfs.c fs/ramfs/ramfs_driver.c fs/procfs/procfs.c fs/devfs/devfs.c \
	fs/sysfs/sysfs.c init/initramfs.c \
	drivers/base/device_model.c drivers/input/keyboard.c \
	drivers/clocksource/timer.c drivers/tty/tty.c drivers/tty/serial.c \
	drivers/video/vga.c drivers/console/console.c drivers/console/console_driver.c
OBJECTS = $(SOURCES_S:.s=.o) $(SOURCES_C:.c=.o)

KERNEL = myos.bin
ISO = myos.iso
INITRD = initrd.img
USER_ELF = user.elf
USER_OBJ = usr/userprog.o
CAT_ELF = cat.elf
CAT_OBJ = usr/catprog.o
USH_ELF = ush.elf
USH_OBJ = usr/ushprog.o
INIT_ELF = init.elf
INIT_OBJ = usr/initprog.o
KTEST_ELF = ktest.elf
KTEST_OBJ = usr/ktestprog.o
USER_ELFS = $(USER_ELF) $(CAT_ELF) $(USH_ELF) $(INIT_ELF) $(KTEST_ELF)

all: $(KERNEL) initramfs.cpio

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

# Create a simple initrd creator tool
mkinitrd:
	@echo "mkinitrd is deprecated, using cpio instead"

initramfs.cpio: $(USER_ELFS)
	mkdir -p rootfs
	echo "Hello, Lite OS!" > rootfs/test.txt
	echo "This is another file." > rootfs/readme.txt
	cp $(USER_ELFS) rootfs/
	cd rootfs && find . | cpio -o -H newc > ../initramfs.cpio
	rm -rf rootfs

$(USER_OBJ): usr/userprog.s
	$(AS) --32 $< -o $@

$(USER_ELF): $(USER_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

$(CAT_OBJ): usr/catprog.s
	$(AS) --32 $< -o $@

$(CAT_ELF): $(CAT_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

$(USH_OBJ): usr/ushprog.s
	$(AS) --32 $< -o $@

$(USH_ELF): $(USH_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

$(INIT_OBJ): usr/initprog.s
	$(AS) --32 $< -o $@

$(INIT_ELF): $(INIT_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

$(KTEST_OBJ): usr/ktestprog.s
	$(AS) --32 $< -o $@

$(KTEST_ELF): $(KTEST_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

run: $(KERNEL) initramfs.cpio
	qemu-system-i386 -kernel $(KERNEL) -initrd initramfs.cpio -m 512M -serial stdio

debug: $(KERNEL) initramfs.cpio
	qemu-system-i386 -kernel $(KERNEL) -initrd initramfs.cpio -m 512M -s -S -serial stdio

smoke:
	bash ./smoke_test.sh

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO) initramfs.cpio $(USER_ELFS) $(USER_OBJ) $(CAT_OBJ) $(USH_OBJ) $(INIT_OBJ) $(KTEST_OBJ)
	rm -rf isodir

.PHONY: all iso run run-iso clean
