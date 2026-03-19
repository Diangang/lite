# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

SOURCES_S = boot.s gdt_flush.s idt_flush.s interrupt.s tss_flush.s
SOURCES_C = kernel.c gdt.c idt.c isr.c keyboard.c shell.c tty.c libc.c timer.c pmm.c vmm.c kheap.c fs.c file.c vfs.c ramfs.c initrd.c task.c syscall.c tss.c procfs.c devfs.c sysfs.c device_model.c
OBJECTS = $(SOURCES_S:.s=.o) $(SOURCES_C:.c=.o)

KERNEL = myos.bin
ISO = myos.iso
INITRD = initrd.img
USER_ELF = user.elf
USER_OBJ = userprog.o
CAT_ELF = cat.elf
CAT_OBJ = catprog.o
USH_ELF = ush.elf
USH_OBJ = ushprog.o
INIT_ELF = init.elf
INIT_OBJ = initprog.o
MMAP_ELF = mmap.elf
MMAP_OBJ = mmaptest.o
FORK_ELF = fork.elf
FORK_OBJ = forktest.o
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
mkinitrd: mkinitrd.c
	gcc -o mkinitrd mkinitrd.c

initrd.img: mkinitrd $(USER_ELFS)
	echo "Hello, Lite OS!" > test.txt
	echo "This is another file." > readme.txt
	./mkinitrd test.txt readme.txt $(USER_ELFS)
	rm -f test.txt readme.txt

$(USER_OBJ): userprog.s
	$(AS) --32 $< -o $@

$(USER_ELF): $(USER_OBJ) userprog.ld
	$(LD) -m elf_i386 -T userprog.ld -o $@ $<

$(CAT_OBJ): catprog.s
	$(AS) --32 $< -o $@

$(CAT_ELF): $(CAT_OBJ) userprog.ld
	$(LD) -m elf_i386 -T userprog.ld -o $@ $<

$(USH_OBJ): ushprog.s
	$(AS) --32 $< -o $@

$(USH_ELF): $(USH_OBJ) userprog.ld
	$(LD) -m elf_i386 -T userprog.ld -o $@ $<

$(INIT_OBJ): initprog.s
	$(AS) --32 $< -o $@

$(INIT_ELF): $(INIT_OBJ) userprog.ld
	$(LD) -m elf_i386 -T userprog.ld -o $@ $<

$(MMAP_OBJ): mmaptest.s
	$(AS) --32 $< -o $@

$(MMAP_ELF): $(MMAP_OBJ) userprog.ld
	$(LD) -m elf_i386 -T userprog.ld -o $@ $<

$(FORK_OBJ): forktest.s
	$(AS) --32 $< -o $@

$(FORK_ELF): $(FORK_OBJ) userprog.ld
	$(LD) -m elf_i386 -T userprog.ld -o $@ $<

run: $(KERNEL) initrd.img
	qemu-system-i386 -kernel $(KERNEL) -initrd initrd.img -m 512M

smoke:
	bash ./smoke_test.sh

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO) mkinitrd initrd.img $(USER_OBJ) $(USER_ELF) $(CAT_OBJ) $(CAT_ELF) $(USH_OBJ) $(USH_ELF) $(INIT_OBJ) $(INIT_ELF) $(MMAP_OBJ) $(MMAP_ELF) $(FORK_OBJ) $(FORK_ELF)
	rm -rf isodir

.PHONY: all iso run run-iso clean
