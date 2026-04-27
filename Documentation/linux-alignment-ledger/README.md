# Linux Alignment Ledger (Auto-Generated)

Scope: arch, block, drivers, fs, include, init, kernel, lib, mm
Linux reference: `linux2.6/`

## Counts
- lite_files: 207
- linux_ref_files: 37955
- lite_unique_functions: 1272
- lite_unique_structs: 158
- lite_unique_globals: 234

## NO_DIRECT_LINUX_MATCH Summary (Top 50 each)
### function
- count: 839
- __bitmap_empty (/data25/lidg/lite/lib/bitmap.c:4)
- __bitmap_full (/data25/lidg/lite/lib/bitmap.c:19)
- address_space_init (/data25/lidg/lite/mm/filemap.c:28)
- address_space_release (/data25/lidg/lite/mm/filemap.c:44)
- align_down (/data25/lidg/lite/fs/exec.c:77)
- align_up (/data25/lidg/lite/fs/exec.c:83)
- alloc_anon_vma_id (/data25/lidg/lite/mm/mmap.c:21)
- alloc_build_scan_control (/data25/lidg/lite/mm/page_alloc.c:23)
- alloc_page (/data25/lidg/lite/include/linux/page_alloc.h:18)
- alloc_reclaim_slowpath (/data25/lidg/lite/mm/page_alloc.c:34)
- alloc_special_inode (/data25/lidg/lite/fs/inode.c:29)
- apic_handle_call_function_ipi (/data25/lidg/lite/arch/x86/kernel/apic.c:44)
- apic_handle_error_event (/data25/lidg/lite/arch/x86/kernel/apic.c:56)
- apic_handle_local_timer (/data25/lidg/lite/arch/x86/kernel/apic.c:21)
- apic_handle_reschedule_ipi (/data25/lidg/lite/arch/x86/kernel/apic.c:32)
- apic_handle_spurious_event (/data25/lidg/lite/arch/x86/kernel/apic.c:68)
- apic_init (/data25/lidg/lite/arch/x86/kernel/apic.c:80)
- apic_install_interrupts (/data25/lidg/lite/arch/x86/kernel/apic.c:109)
- apic_install_ipi_vectors (/data25/lidg/lite/arch/x86/kernel/apic.c:97)
- apic_install_local_event_vectors (/data25/lidg/lite/arch/x86/kernel/apic.c:103)
- apic_install_local_timer_vector (/data25/lidg/lite/arch/x86/kernel/apic.c:92)
- balance_dirty_pages_lite (/data25/lidg/lite/mm/filemap.c:19)
- bdev_alloc_for_disk (/data25/lidg/lite/block/blkdev.c:304)
- bdev_lookup (/data25/lidg/lite/block/blkdev.c:236)
- bdev_register (/data25/lidg/lite/block/blkdev.c:254)
- bdev_unregister (/data25/lidg/lite/block/blkdev.c:266)
- bh_all_add_tail (/data25/lidg/lite/fs/buffer.c:38)
- bh_all_del (/data25/lidg/lite/fs/buffer.c:50)
- bh_alloc (/data25/lidg/lite/fs/buffer.c:108)
- bh_evict_one (/data25/lidg/lite/fs/buffer.c:88)
- bh_hash_insert (/data25/lidg/lite/fs/buffer.c:65)
- bh_hash_remove (/data25/lidg/lite/fs/buffer.c:73)
- bh_hashfn (/data25/lidg/lite/fs/buffer.c:16)
- bh_lookup (/data25/lidg/lite/fs/buffer.c:25)
- bio_endio_sync (/data25/lidg/lite/block/blkdev.c:466)
- blk_irqs_enabled (/data25/lidg/lite/block/blk-core.c:8)
- blk_sysfs_append (/data25/lidg/lite/block/blkdev.c:31)
- blk_sysfs_append_ch (/data25/lidg/lite/block/blkdev.c:44)
- blk_sysfs_append_u32 (/data25/lidg/lite/block/blkdev.c:56)
- blk_sysfs_emit_u32_line (/data25/lidg/lite/block/blkdev.c:18)
- blkdev_release (/data25/lidg/lite/fs/block_dev.c:49)
- block_account_io (/data25/lidg/lite/block/blkdev.c:519)
- block_attr_show_queue_nr_requests (/data25/lidg/lite/block/blkdev.c:127)
- block_attr_show_size (/data25/lidg/lite/block/blkdev.c:63)
- block_attr_show_stat (/data25/lidg/lite/block/blkdev.c:77)
- block_device_read (/data25/lidg/lite/block/blkdev.c:473)
- block_device_write (/data25/lidg/lite/block/blkdev.c:496)
- block_register_disk (/data25/lidg/lite/block/blkdev.c:402)
- blockdev_inode_create (/data25/lidg/lite/fs/block_dev.c:67)
- blockdev_inode_destroy (/data25/lidg/lite/fs/block_dev.c:95)

### struct
- count: 31
- bdev_map_entry (/data25/lidg/lite/block/blkdev.c:219)
- bootmem_data (/data25/lidg/lite/mm/bootmem.c:27)
- bootmem_e820_entry (/data25/lidg/lite/mm/bootmem.c:16)
- bootmem_module_range (/data25/lidg/lite/mm/bootmem.c:22)
- bootmem_region (/data25/lidg/lite/mm/bootmem.c:11)
- child_name_match (/data25/lidg/lite/drivers/base/virtual.c:8)
- cpio_newc_header (/data25/lidg/lite/init/initramfs.c:14)
- dcache_hash (/data25/lidg/lite/fs/dentry.c:10)
- dirent (/data25/lidg/lite/include/linux/fs.h:62)
- disk_map_entry (/data25/lidg/lite/block/blkdev.c:228)
- kthread_create_info (/data25/lidg/lite/kernel/kthread.c:9)
- large_hdr (/data25/lidg/lite/mm/slab.c:26)
- minix_inode_disk (/data25/lidg/lite/fs/minixfs/minixfs.c:26)
- minix_mount_data (/data25/lidg/lite/fs/minixfs/minixfs.c:48)
- multiboot_info (/data25/lidg/lite/include/asm/multiboot.h:8)
- multiboot_memory_map (/data25/lidg/lite/include/asm/multiboot.h:35)
- multiboot_module (/data25/lidg/lite/include/asm/multiboot.h:44)
- page_cache_entry (/data25/lidg/lite/include/linux/pagemap.h:16)
- pglist_data (/data25/lidg/lite/include/linux/mmzone.h:58)
- proc_seq_state (/data25/lidg/lite/fs/procfs/procfs.c:152)
- ramdisk_backend (/data25/lidg/lite/drivers/block/ramdisk.c:12)
- req (/data25/lidg/lite/drivers/base/devtmpfs.c:22)
- sched_cpu_state (/data25/lidg/lite/kernel/sched.c:31)
- slab (/data25/lidg/lite/mm/slab.c:12)
- subsystem (/data25/lidg/lite/include/linux/kobject.h:36)
- swap_slot (/data25/lidg/lite/mm/swap.c:15)
- sysfs_dirent (/data25/lidg/lite/fs/sysfs/sysfs.c:27)
- sysfs_named_inode (/data25/lidg/lite/fs/sysfs/sysfs.c:1042)
- virtqueue_buf (/data25/lidg/lite/include/linux/virtio.h:81)
- vmalloc_block (/data25/lidg/lite/mm/vmalloc.c:9)
- vsn_ctx (/data25/lidg/lite/lib/vsprintf.c:7)

### global
- count: 198
- apic_last_call_vector (/data25/lidg/lite/arch/x86/kernel/apic.c:17)
- apic_last_error_vector (/data25/lidg/lite/arch/x86/kernel/apic.c:18)
- apic_last_reschedule_vector (/data25/lidg/lite/arch/x86/kernel/apic.c:16)
- apic_last_spurious_vector (/data25/lidg/lite/arch/x86/kernel/apic.c:19)
- apic_timer_irqs (/data25/lidg/lite/arch/x86/kernel/apic.c:11)
- bdev_map_count (/data25/lidg/lite/block/blkdev.c:225)
- bh_all_head (/data25/lidg/lite/fs/buffer.c:12)
- bh_all_tail (/data25/lidg/lite/fs/buffer.c:13)
- bh_hash (/data25/lidg/lite/fs/buffer.c:11)
- bh_total (/data25/lidg/lite/fs/buffer.c:14)
- blk_bytes_read (/data25/lidg/lite/block/blkdev.c:14)
- blk_bytes_written (/data25/lidg/lite/block/blkdev.c:15)
- blk_reads (/data25/lidg/lite/block/blkdev.c:12)
- blk_writes (/data25/lidg/lite/block/blkdev.c:13)
- boot_cpu_sched (/data25/lidg/lite/kernel/sched.c:37)
- boot_mbi (/data25/lidg/lite/include/asm/multiboot.h:51)
- bootmem (/data25/lidg/lite/mm/bootmem.c:50)
- buddy_max_order (/data25/lidg/lite/mm/page_alloc.c:13)
- buddy_next (/data25/lidg/lite/mm/page_alloc.c:12)
- buddy_ready (/data25/lidg/lite/mm/page_alloc.c:14)
- cache_sizes (/data25/lidg/lite/mm/slab.c:33)
- cached_mbi (/data25/lidg/lite/mm/page_alloc.c:8)
- caches (/data25/lidg/lite/mm/slab.c:34)
- console_list (/data25/lidg/lite/kernel/printk.c:9)
- contig_zonelist (/data25/lidg/lite/include/linux/mmzone.h:75)
- cow_copies (/data25/lidg/lite/mm/memory.c:19)
- cow_faults (/data25/lidg/lite/mm/memory.c:18)
- current (/data25/lidg/lite/include/linux/sched.h:77)
- deferred_devs (/data25/lidg/lite/drivers/base/driver.c:127)
- deferred_devs_count (/data25/lidg/lite/drivers/base/driver.c:128)
- dev_fs_type (/data25/lidg/lite/drivers/base/devtmpfs.c:20)
- devtmpfs_console_inode (/data25/lidg/lite/drivers/base/devtmpfs.c:15)
- devtmpfs_root (/data25/lidg/lite/drivers/base/devtmpfs.c:13)
- devtmpfs_sb (/data25/lidg/lite/drivers/base/devtmpfs.c:14)
- devtmpfs_tty_inode (/data25/lidg/lite/drivers/base/devtmpfs.c:16)
- disk_map (/data25/lidg/lite/block/blkdev.c:233)
- disk_map_count (/data25/lidg/lite/block/blkdev.c:234)
- dma_zonelist (/data25/lidg/lite/include/linux/mmzone.h:76)
- drv_attr_bind (/data25/lidg/lite/drivers/base/driver.c:7)
- drv_attr_name (/data25/lidg/lite/drivers/base/driver.c:6)
- drv_attr_unbind (/data25/lidg/lite/drivers/base/driver.c:8)
- foreground_pid (/data25/lidg/lite/drivers/tty/tty_io.c:27)
- gdt_descr (/data25/lidg/lite/arch/x86/kernel/gdt.c:49)
- gdt_table (/data25/lidg/lite/arch/x86/kernel/gdt.c:50)
- generic_dirent (/data25/lidg/lite/fs/readdir.c:166)
- i8042_initialized (/data25/lidg/lite/drivers/input/serio/i8042.c:8)
- i8042_pdev (/data25/lidg/lite/drivers/input/serio/i8042.c:9)
- i8042_port (/data25/lidg/lite/drivers/input/serio/i8042.c:7)
- input_count (/data25/lidg/lite/drivers/tty/n_tty.c:14)
- input_head (/data25/lidg/lite/drivers/tty/n_tty.c:12)

## File Path Mapping
- linux2.6 path match: 134
- linux2.6 path NO match: 73

## Initcall Summary (Lite)
- core_initcall: 5
- device_initcall: 3
- fs_initcall: 3
- late_initcall: 1
- module_init: 8
- subsys_initcall: 9

## Initcall Name NO_DIRECT_LINUX_MATCH (Top 50)
- count: 13
- nvme_class_init
- pci_init
- proc_root_init
- ramdisk_init
- scsi_init_hosts
- scsi_sysfs_register
- sd_disk_class_init
- serial8250_driver_init
- serio_core_init
- tty_init
- virtio_pci_init
- virtio_scsi_init
