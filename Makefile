# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin \
	-Iinclude -Ikernel -Ilib -Iarch/x86 -Imm -Ifs -Idrivers -Idrivers/base -Idrivers/input -Idrivers/tty -Idrivers/clock -Idrivers/vga -Idrivers/console
LDFLAGS = -m elf_i386 -T arch/x86/linker.ld -nostdlib

SOURCES_S = arch/x86/boot.s arch/x86/interrupt.s
SOURCES_C = kernel/kernel.c kernel/syscall.c kernel/task.c kernel/shell.c \
	arch/x86/gdt.c arch/x86/idt.c arch/x86/isr.c \
	mm/mm.c mm/pmm.c mm/vmm.c mm/kheap.c mm/filemap.c lib/libc.c \
        fs/file.c fs/inode.c fs/dentry.c fs/namei.c fs/read_write.c fs/open.c fs/readdir.c fs/ioctl.c fs/namespace.c fs/ramfs/ramfs.c fs/initrd/initrd.c fs/procfs/procfs.c fs/devfs/devfs.c fs/sysfs/sysfs.c \
        drivers/base/device_model.c drivers/input/keyboard.c drivers/clock/timer.c drivers/tty/tty.c drivers/tty/serial.c drivers/vga/vga.c drivers/console/console.c
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
MMAP_ELF = mmap.elf
MMAP_OBJ = usr/mmaptest.o
FORK_ELF = fork.elf
FORK_OBJ = usr/forktest.o
USER_ELFS = $(USER_ELF) $(CAT_ELF) $(USH_ELF) $(INIT_ELF) $(MMAP_ELF) $(FORK_ELF)

all: $(KERNEL) $(INITRD)

$(KERNEL): $(OBJECTS)
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
mkinitrd: tools/mkinitrd.c
	gcc -o mkinitrd tools/mkinitrd.c

initrd.img: mkinitrd $(USER_ELFS)
	echo "Hello, Lite OS!" > test.txt
	echo "This is another file." > readme.txt
	./mkinitrd test.txt readme.txt $(USER_ELFS)
	rm -f test.txt readme.txt

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

$(MMAP_OBJ): usr/mmaptest.s
	$(AS) --32 $< -o $@

$(MMAP_ELF): $(MMAP_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

$(FORK_OBJ): usr/forktest.s
	$(AS) --32 $< -o $@

$(FORK_ELF): $(FORK_OBJ) usr/userprog.ld
	$(LD) -m elf_i386 -T usr/userprog.ld -o $@ $<

run: $(KERNEL) initrd.img
	qemu-system-i386 -kernel $(KERNEL) -initrd initrd.img -m 512M

smoke:
	bash ./smoke_test.sh

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO) mkinitrd initrd.img $(USER_OBJ) $(USER_ELF) $(CAT_OBJ) $(CAT_ELF) $(USH_OBJ) $(USH_ELF) $(INIT_OBJ) $(INIT_ELF) $(MMAP_OBJ) $(MMAP_ELF) $(FORK_OBJ) $(FORK_ELF)
	rm -rf isodir

.PHONY: all iso run run-iso clean
