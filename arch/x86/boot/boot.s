/*
 * boot.s - Early x86 kernel entry.
 *
 * This file runs before any C code. Its job is to:
 * 1. expose a Multiboot header for the bootloader;
 * 2. save the Multiboot magic/info pointer;
 * 3. install a minimal GDT;
 * 4. build the earliest page tables;
 * 5. enable paging and jump into the higher-half kernel;
 * 6. call the C entry point once the virtual address space is usable.
 *
 * Register cheat sheet for this file:
 * - %eax:
 *     * on entry: Multiboot magic;
 *     * later: temporary arithmetic register, CR0/CR3 helper, current physical page;
 *     * before calling C: restored to the saved magic value.
 * - %ebx:
 *     * on entry: physical pointer to struct multiboot_info;
 *     * later: loop counter / preserved page-table count;
 *     * before calling C: rebuilt as the higher-half mbi pointer argument.
 * - %ecx:
 *     * usually loop counter for rep/loop instructions;
 *     * also temporarily holds mods_count or computed page-table count.
 * - %edx:
 *     * scratch register for PTE/PDE values and temporary max comparisons.
 * - %esi:
 *     * scratch pointer, mainly used for the multiboot_info pointer or PT base.
 * - %edi:
 *     * destination pointer for memory clears/fills (page directory/page tables).
 * - %ebp:
 *     * holds the running "highest physical address we must map" value.
 *
 * Address cheat sheet for this file:
 * - linked kernel symbols (for example stack_top, boot_page_directory, end) live
 *   in the higher half near 0xC0000000;
 * - before paging is enabled, the CPU still needs physical addresses, so this file
 *   often does "sub $KERNEL_BASE" to convert a linked higher-half symbol into the
 *   physical address where the object actually resides;
 * - after paging is enabled and execution jumps to trampoline_high, the kernel runs
 *   through higher-half virtual addresses and no longer needs the low identity alias.
 */

/* Multiboot header constants */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO  /* this is the Multiboot 'flag' field */
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */
.set INIT_MAP_BEYOND_END, 0x02000000 /* extra physical bytes mapped beyond early-boot needs */
.set BOOT_PT_MAX, 256                 /* maximum number of 4MB page tables (256*4MB = 1GB) */

/* Declare a header as in the Multiboot Standard. */
.section .multiboot                     # Multiboot header section
.align 4                                # Required alignment
.long MAGIC                             # Multiboot magic
.long FLAGS                             # Requested features
.long CHECKSUM                          # MAGIC+FLAGS+CHECKSUM == 0

/* Reserve the bootstrap stack used before the scheduler exists. */
.section .bootstrap_stack, "aw", @nobits # Early stack (no file content)
.align 16                                 # ABI-friendly stack alignment
stack_bottom:                              # Low end of bootstrap stack
.skip 32768 # 32 KiB                       # Reserve stack storage
stack_top:                                 # High end (initial %esp)

.section .text.boot                      # Early text (runs before C)
.global _start                           # Export Multiboot entry
.type _start, @function                  # Mark symbol type for tools
/* _start: Entry point reached directly from the Multiboot bootloader. */
_start:                                  # CPU starts here
	.set KERNEL_BASE, 0xC0000000       # Kernel higher-half base (3G split)
	/*
	 * The bootloader has loaded us into 32-bit protected mode on a x86
	 * machine. Interrupts are disabled. Paging is disabled. The processor
	 * state is as defined in the multiboot standard.
	 */

	/*
	 * Symbols are linked in the higher half, but paging is still off here.
	 * Subtract KERNEL_BASE so %esp points at the physical bootstrap stack.
	 */
	mov $stack_top, %esp                # %esp = linked VA of stack_top
	sub $KERNEL_BASE, %esp              # %esp -> physical stack address
	/* Save the Multiboot handoff values before we start rewriting state. */
	mov $boot_magic, %edi               # %edi = linked VA of boot_magic
	sub $KERNEL_BASE, %edi              # %edi -> physical boot_magic slot
	mov %eax, (%edi)                    # Save multiboot magic from %eax
	mov $boot_mbi, %edi                 # %edi = linked VA of boot_mbi
	sub $KERNEL_BASE, %edi              # %edi -> physical boot_mbi slot
	mov %ebx, (%edi)                    # Save mbi pointer from %ebx
	/* Install a flat GDT so the transition into paging uses known segments. */
	lgdt gdt_descriptor                  # Load GDTR with our minimal GDT
	mov $0x10, %ax                       # Kernel data selector
	mov %ax, %ds                         # Load DS
	mov %ax, %es                         # Load ES
	mov %ax, %fs                         # Load FS
	mov %ax, %gs                         # Load GS
	mov %ax, %ss                         # Load SS
	ljmp $0x08, $1f                      # Far jump to reload CS
1:                                         # Local label (after CS reload)

	/* Clear the early page directory page. */
	mov $boot_page_directory, %edi       # %edi = linked VA of pgdir
	sub $KERNEL_BASE, %edi               # %edi -> physical pgdir page
	xor %eax, %eax                       # Fill value = 0
	mov $1024, %ecx                      # 1024 dwords = 4KB
	cld                                   # Ensure forward stores
	rep stosl                             # Zero the page directory

	/*
	 * Determine how much physical memory must be mapped into the higher half
	 * before we can safely enter C:
	 * - the kernel image itself (up to 'end');
	 * - multiboot memory map array (mmap_addr..mmap_addr+mmap_length);
	 * - multiboot modules array (mods_addr..mods_addr+mods_count*sizeof(module));
	 * - each module payload end address (mod_end).
	 *
	 * The result is rounded up to 4MB so we can map it with whole page tables.
	 * This is closer to Linux 2.6 head.S style ("map up to _end plus some slack")
	 * than a fixed 128MB mapping.
	 */
	mov $boot_mbi, %esi                  # %esi = linked VA of boot_mbi slot
	sub $KERNEL_BASE, %esi               # %esi -> physical slot
	mov (%esi), %esi                     # %esi = mbi physical pointer
	mov $end, %ebp                       # %ebp = linked VA of kernel end
	sub $KERNEL_BASE, %ebp               # %ebp = kernel end physical address

	mov 44(%esi), %eax                   # %eax = mmap_length
	mov 48(%esi), %edx                   # %edx = mmap_addr
	add %edx, %eax                       # %eax = mmap_end
	cmpl %eax, %ebp                      # max(end, mmap_end)
	jae 4f                               # If kernel end is already higher, skip update
	mov %eax, %ebp                       # Otherwise extend max to mmap_end
4:                                         # Continue after mmap_end max check

	mov 20(%esi), %ecx                   # %ecx = mods_count
	mov 24(%esi), %edx                   # %edx = mods_addr
	test %ecx, %ecx                      # any modules?
	jz 6f                                # If no modules exist, skip module scan

	mov %ecx, %eax                       # %eax = mods_count
	shl $4, %eax                         # %eax = mods_count * 16 (sizeof(module))
	add %edx, %eax                       # %eax = end of module array
	cmpl %eax, %ebp                      # max(end, mods_struct_end)
	jae 5f                               # If current max is already higher, keep it
	mov %eax, %ebp                       # Otherwise extend max to module-array end
5:                                         # Continue after module-array max check

	mov %ecx, %ebx                       # %ebx = loop count
	mov %edx, %eax                       # %eax = module array pointer
7:                                         # Loop over each multiboot module entry
	mov 4(%eax), %edx                    # %edx = mod_end
	cmpl %edx, %ebp                      # max(end, mod_end)
	jae 8f                               # If current max is already higher, keep it
	mov %edx, %ebp                       # Otherwise extend max to this module end
8:                                         # Continue with next module
	add $16, %eax                        # next module
	decl %ebx                            # One module processed
	jnz 7b                               # Loop until all modules are scanned
6:                                         # Continue after optional module scan

	add $INIT_MAP_BEYOND_END, %ebp       # map some extra past max requirement
	add $0x003FFFFF, %ebp                # round up to 4MB
	and $0xFFC00000, %ebp                # align down to 4MB boundary
	mov %ebp, %ecx                       # %ecx = bytes_to_map (rounded)
	shr $22, %ecx                        # %ecx = number of 4MB page tables
	cmpl $1, %ecx                        # at least one PT
	jae 9f                               # If already >= 1 PT, keep the count
	mov $1, %ecx                         # Otherwise force a minimum of one PT
9:                                         # Continue after minimum-PT clamp
	cmpl $BOOT_PT_MAX, %ecx              # cap to BOOT_PT_MAX
	jbe 10f                              # If within cap, keep the count
	mov $BOOT_PT_MAX, %ecx               # Otherwise clamp to BOOT_PT_MAX
10:                                        # Continue after maximum-PT clamp
	mov %ecx, %ebx                       # %ebx = pt_count (preserve across later loops)

	/*
	 * Build a single 4MB identity map:
	 *   VA 0x00000000..0x003FFFFF -> PA 0x00000000..0x003FFFFF
	 *
	 * This temporary mapping is only a runway for the moment when paging is
	 * enabled: the CPU is still executing at low addresses at that instant.
	 */
	mov $boot_page_table_low, %edi       # %edi = linked VA of low PT
	sub $KERNEL_BASE, %edi               # %edi -> physical low PT
	xor %eax, %eax                       # Start at PA 0
	mov $1024, %ecx                      # 1024 PTEs
	rep stosl                             # Zero the low PT
	mov $boot_page_table_low, %edi       # %edi = linked VA of low PT
	sub $KERNEL_BASE, %edi               # %edi -> physical low PT
	xor %eax, %eax                       # PA cursor = 0
	mov $1024, %ecx                      # Fill 1024 entries
1:                                         # Local loop for low PT fill
	mov %eax, %edx                       # %edx = current PA
	or $0x3, %edx                        # Add PRESENT|RW
	mov %edx, (%edi)                     # Store PTE
	add $0x1000, %eax                    # Next 4KB page
	add $4, %edi                         # Next PTE slot
	loop 1b                               # Repeat 1024 times

	/*
	 * Build page tables for the higher-half kernel direct map:
	 *   VA 0xC0000000..(0xC0000000+bytes_to_map) -> PA 0x00000000..bytes_to_map
	 *
	 * The number of page tables (4MB each) was computed above in %ecx.
	 * This gives the kernel a stable virtual home immediately after paging is
	 * turned on. The first 4MB of physical memory therefore has two aliases
	 * during the transition: one low identity alias and one higher-half alias.
	 */
	mov $boot_page_tables, %edi          # %edi = linked VA of PT array
	sub $KERNEL_BASE, %edi               # %edi -> physical PT array
	xor %eax, %eax                       # PA cursor = 0
	mov %ebx, %ecx                       # %ecx = number of PTs
	shl $10, %ecx                        # %ecx = number of PTEs (PTs*1024)
2:                                         # Local loop for high PT fill
	mov %eax, %edx                       # %edx = current PA
	or $0x3, %edx                        # Add PRESENT|RW
	mov %edx, (%edi)                     # Store PTE
	add $0x1000, %eax                    # Next 4KB page
	add $4, %edi                         # Next PTE slot
	loop 2b                               # Repeat for pt_count*4MB

	/* PDE[0] covers the low 4MB identity mapping. */
	mov $boot_page_directory, %edi       # %edi = linked VA of pgdir
	sub $KERNEL_BASE, %edi               # %edi -> physical pgdir
	mov $boot_page_table_low, %eax       # %eax = linked VA of low PT
	sub $KERNEL_BASE, %eax               # %eax -> physical low PT
	or $0x3, %eax                        # Add PRESENT|RW
	mov %eax, (%edi)                     # pgdir[0] = low PT

	/*
	 * PDE[768] starts at 0xC0000000, so PDE[768..] maps physical memory into
	 * the higher half. We map PT-count entries (computed above) starting at 768.
	 */
	mov $boot_page_tables, %eax          # %eax = linked VA of PT array
	sub $KERNEL_BASE, %eax               # %eax -> physical PT array
	mov $0, %edx                         # PT index = 0..(pt_count-1)
3:                                         # Local loop for PDE[768..799]
	mov %eax, %esi                       # %esi = current PT physical base
	or $0x3, %esi                        # Add PRESENT|RW
	mov %esi, 768*4(%edi,%edx,4)         # pgdir[768+idx] = PT
	add $4096, %eax                      # Next PT (4KB)
	inc %edx                             # Next PDE slot index
	cmpl %ebx, %edx                      # compare idx vs pt_count
	jl 3b                                # Loop until idx==pt_count

	/*
	 * Turn paging on with the early directory. After CR0.PG is set, virtual
	 * addresses are interpreted through the page tables we just built.
	 */
	mov $boot_page_directory, %eax       # %eax = linked VA of pgdir
	sub $KERNEL_BASE, %eax               # %eax -> physical pgdir
	mov %eax, %cr3                       # Install CR3 (page directory)
	mov %cr0, %eax                       # Read CR0
	or $0x80000000, %eax                 # Set CR0.PG (enable paging)
	mov %eax, %cr0                       # Write CR0 back

	/*
	 * Jump to the higher-half alias of trampoline_high. From this point on we
	 * intend to execute kernel code via higher-half virtual addresses.
	 */
	mov $trampoline_high, %eax           # %eax = linked VA label (low)
	add $KERNEL_BASE, %eax               # Convert to higher-half VA
	jmp *%eax                             # Switch execution into high half

/*
 * trampoline_high:
 * We are now executing through the higher-half mapping. Remove PDE[0] so the
 * temporary low identity alias disappears, reload CR3 to flush the TLB, and
 * then enter C code. start_kernel() therefore runs with paging already enabled
 * and with the kernel executing in higher-half virtual addresses.
 */
trampoline_high:                           # First instruction in high half
	mov $stack_top, %esp                # %esp = high-half bootstrap stack
	mov $boot_page_directory, %edi      # %edi = linked VA of pgdir
	movl $0, (%edi)                     # Clear PDE[0] (drop low alias)
	mov $boot_page_directory, %eax      # %eax = linked VA of pgdir
	sub $KERNEL_BASE, %eax              # %eax -> physical pgdir
	mov %eax, %cr3                      # Reload CR3 to flush TLB

	/* Rebuild the C call arguments and pass them to kernel_entry/start_kernel. */
	mov boot_mbi, %ebx                  # %ebx = physical mbi pointer
	add $KERNEL_BASE, %ebx              # Convert to higher-half VA
	mov boot_magic, %eax                # %eax = multiboot magic
	push %ebx                           # Arg2: mbi pointer
	push %eax                           # Arg1: magic
	mov $kernel_entry, %eax             # C trampoline entry
	call *%eax                          # Enter C world (never returns)

	/* start_kernel() is not expected to return. Halt forever if it does. */
	cli                                 # Disable interrupts
1:	hlt                                 # Halt CPU until interrupt
	jmp 1b                              # Infinite loop

/* Record the size of _start for debugging and symbol inspection. */
.size _start, . - _start                # ELF symbol size

.section .bss                           # Zero-initialized data
.align 4096                             # Page alignment for tables
.global boot_page_directory             # Export pgdir symbol
/* One 4KB page directory used only during early boot. */
boot_page_directory:
	.skip 4096                          # Reserve one pgdir page
.align 4096                             # Next page-aligned object
/* One page table for the low 4MB identity map. */
boot_page_table_low:
	.skip 4096                          # Reserve one PT page
.align 4096                             # Next page-aligned object
/* Backing storage for higher-half page tables (only the first pt_count are used). */
boot_page_tables:
	.skip 4096*BOOT_PT_MAX               # Reserve BOOT_PT_MAX PT pages
/* Saved Multiboot magic value from %eax. */
boot_magic:
	.long 0                              # Filled by _start
/* Saved Multiboot info pointer from %ebx. */
boot_mbi:
	.long 0                              # Filled by _start

.section .text.boot                      # Back to early text
.align 8                                 # GDT wants 8-byte alignment
/* Minimal flat GDT: null, kernel code, kernel data. */
gdt_start:
	.quad 0x0000000000000000            # Null descriptor
	.quad 0x00CF9A000000FFFF            # Kernel code segment
	.quad 0x00CF92000000FFFF            # Kernel data segment
gdt_end:                                 # End marker used to compute GDT size

/* GDTR operand used by lgdt above. */
gdt_descriptor:
	.word gdt_end - gdt_start - 1       # Limit = size - 1
	.long gdt_start                     # Base = GDT address
