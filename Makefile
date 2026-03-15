# Makefile for Minimal Kernel (Multiboot)

CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-builtin
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

SOURCES_S = boot.s
SOURCES_C = kernel.c
OBJECTS = $(SOURCES_S:.s=.o) $(SOURCES_C:.c=.o)

KERNEL = myos.bin
ISO = myos.iso

all: $(KERNEL)

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

# Run directly with QEMU's built-in multiboot loader
run: $(KERNEL)
	@echo "\nKernel build complete: $(KERNEL)"
	@echo "To run directly with QEMU, execute manually:"
	@echo "qemu-system-i386 -kernel $(KERNEL) -curses"

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO)
	rm -rf isodir

.PHONY: all iso run run-iso clean
