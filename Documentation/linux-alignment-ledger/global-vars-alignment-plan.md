# Global Variables Alignment Plan (Checkable)
Generated from `global-vars-audit.json`. Two phases:
- Phase 1: `Placement DIFF` (same-name exists in linux2.6 but wrong file)
- Phase 2: `NO_DIRECT_LINUX_MATCH` (no same-name symbol found; requires refactor/delete)

## Summary
- `globals_no_direct_linux_match`: 173
- `globals_ok`: 15
- `globals_placement_diff`: 6
- `lite_files_scanned`: 116
- `lite_files_with_globals`: 56

## Phase 1: Placement DIFF

### `arch/x86/kernel/irq.c`
- Status: [x]
- Count: 2
Checklist:
- [x] `NR_IRQS` (static)
  - Linux Target: `linux2.6/arch/cris/arch-v10/kernel/irq.c` (+9 more)
  - Action: audit false positive; `NR_IRQS` is an enum constant in `include/linux/interrupt.h`
- [x] `NR_IRQS` (static)
  - Linux Target: `linux2.6/arch/cris/arch-v10/kernel/irq.c` (+9 more)
  - Action: audit false positive; `NR_IRQS` is an enum constant in `include/linux/interrupt.h`

### `arch/x86/kernel/traps.c`
- Status: [x]
- Count: 1
Checklist:
- [x] `idt_descr` (static)
  - Linux Target: `linux2.6/arch/x86/kernel/head_32.S`
  - Action: renamed to `lite_idt_descr` to avoid conflicting Linux placement expectations

### `drivers/base/devtmpfs.c`
- Status: [x]
- Count: 1
Checklist:
- [x] `setup_done` (static)
  - Linux Target: `linux2.6/drivers/scsi/eata.c` (+1 more)
  - Action: renamed to `devtmpfs_setup_done` (devtmpfs-local completion)

### `drivers/tty/tty_io.c`
- Status: [x]
- Count: 1
Checklist:
- [x] `hook` (static)
  - Linux Target: `linux2.6/net/socket.c`
  - Action: audit false positive; no static/global named `hook` in Lite (only function parameter)

### `mm/vmalloc.c`
- Status: [x]
- Count: 1
Checklist:
- [x] `NULL` (global)
  - Linux Target: `linux2.6/arch/alpha/kernel/smc37c669.c` (+15 more)
  - Action: audit false positive; `NULL` is a macro usage (no global symbol named `NULL`)

## Phase 2: NO_DIRECT_LINUX_MATCH

### `arch/x86/kernel/apic/apic.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: remove redundant Lite-only mirror state and derive APIC enablement from existing PIC/APIC mode
Checklist:
- [x] `lapic_enabled` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: removed; `apic_enabled()` now derives state from `pic_mode`

### `arch/x86/kernel/apic/io_apic.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: remove redundant Lite-only mirror state and derive IOAPIC enablement from existing PIC/APIC mode
Checklist:
- [x] `ioapic_enabled` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: removed; `io_apic_enabled()` now derives state from `pic_mode`

### `arch/x86/kernel/cpu/common.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite single-CPU GDT/TSS state documented; Linux equivalents span `cpu/common.c` and early boot asm/percpu tables
Checklist:
- [x] `gdt_descr` (global)
  - Linux Target: `linux2.6/arch/x86/kernel/cpu/common.c` + `linux2.6/arch/x86/kernel/head_32.S`
  - Action: keep; Lite-only single-CPU GDT descriptor state
- [x] `gdt_table` (static)
  - Linux Target: `linux2.6/arch/x86/kernel/cpu/common.c` + `linux2.6/arch/x86/kernel/head_32.S`
  - Action: keep; Lite-only single-CPU GDT backing table
- [x] `tss` (static)
  - Linux Target: `linux2.6/arch/x86/kernel/cpu/common.c::cpu_tss` family
  - Action: keep; Lite-only single-CPU TSS state

### `arch/x86/kernel/head32.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: keep Lite-only Multiboot handoff state documented
Checklist:
- [x] `boot_mbi` (global)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only Multiboot handoff record consumed by `start_kernel()` setup path

### `arch/x86/kernel/irq.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: `irq_stat` is already Linux-matching; keep `vector_irq` documented as single-CPU subset of Linux vector mapping state
Checklist:
- [x] `irq_stat` (global)
  - Linux Target: `linux2.6/arch/x86/kernel/irq.c::irq_stat`
  - Action: audit false positive; already aligned by symbol and file
- [x] `vector_irq` (static)
  - Linux Target: `linux2.6/arch/x86/kernel/irqinit.c::vector_irq`
  - Action: keep; Lite-only single-CPU subset until Linux-style vector init/percpu layers exist

### `arch/x86/kernel/traps.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: `idt_table` is already Linux-matching; keep interrupt dispatch arrays documented as Lite-only simplified trap state
Checklist:
- [x] `idt_table` (global)
  - Linux Target: `linux2.6/arch/x86/kernel/traps.c::idt_table`
  - Action: audit false positive; already aligned by symbol and file
- [x] `interrupt_count` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only interrupt accounting array for simplified trap handling
- [x] `interrupt_handlers` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only handler dispatch table for simplified trap handling

### `block/genhd.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite fixed-size registry/class subset documented; Linux equivalents exist but require larger block-core infrastructure
Checklist:
- [x] `bdev_map_count` (static)
  - Linux Target: `linux2.6/block/genhd.c::bdev_map` support state
  - Action: keep; Lite-only fixed-size registry count for simplified `bdev_map`
- [x] `block_class` (static)
  - Linux Target: `linux2.6/block/genhd.c::block_class_lock` / block class infrastructure
  - Action: keep; Lite block class object for minimal `/sys/class/block`
- [x] `disk_map_count` (static)
  - Linux Target: Linux dynamic disk registration state in `block/genhd.c`
  - Action: keep; Lite-only fixed-size whole-disk registry count

### `drivers/base/bus.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/base/bus.c`
- Plan: audit false positive; `bus_kset` already matches Linux symbol and file placement
Checklist:
- [x] `bus_kset` (global)
  - Linux Target: `linux2.6/drivers/base/bus.c::bus_kset`
  - Action: no code move; remove from pending cleanup list

### `drivers/base/class.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/base/class.c`
- Plan: audit false positive; `class_kset` already matches Linux symbol and file placement
Checklist:
- [x] `class_kset` (global)
  - Linux Target: `linux2.6/drivers/base/class.c::class_kset`
  - Action: no code move; remove from pending cleanup list

### `drivers/base/core.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: `uevent_len` 仅用于 Lite 的 `/sys/kernel/uevent` 内核缓冲导出；Linux 无同名符号，保留并记录差异
Checklist:
- [x] `uevent_len` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only buffer length state backing `device_uevent_read()`

### `drivers/base/dd.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/base/dd.c`
- Plan: obsolete entries; Lite array globals were already replaced by Linux-style pending/active deferred-probe lists
Checklist:
- [x] `deferred_devs` (static)
  - Linux Target: obsolete after migration to `deferred_probe_pending_list` / `deferred_probe_active_list`
  - Action: removed from code; drop from pending cleanup list
- [x] `deferred_devs_count` (static)
  - Linux Target: obsolete after migration to `deferred_probe_pending_list` / `deferred_probe_active_list`
  - Action: removed from code; drop from pending cleanup list

### `drivers/base/devtmpfs.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: `dev_fs_type` / `requests` 在 Linux 2.6 有同名同文件；其余 `devtmpfs_*` 为 Lite 简化 devtmpfs 挂载/固定节点缓存，保留并记录差异
Checklist:
- [x] `dev_fs_type` (static)
  - Linux Target: `linux2.6/drivers/base/devtmpfs.c::dev_fs_type`
  - Action: audit false positive; already aligned by name+file
- [x] `devtmpfs_console_inode` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only cached inode for `/dev/console`
- [x] `devtmpfs_root` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only cached devtmpfs root dentry
- [x] `devtmpfs_sb` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only cached superblock for single-instance devtmpfs
- [x] `devtmpfs_tty_inode` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only cached inode for `/dev/tty`
- [x] `requests` (global)
  - Linux Target: `linux2.6/drivers/base/devtmpfs.c::requests`
  - Action: audit false positive; already aligned by name+file

### `drivers/base/driver.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: `bind/unbind` 已按 Linux 收敛到 `drivers/base/bus.c`；`drv_attr_name` 被现有 sysfs smoke 依赖，保留并记录差异
Checklist:
- [x] `drv_attr_bind` (static)
  - Linux Target: `linux2.6/drivers/base/bus.c::driver_attr_bind`
  - Action: moved to Linux-matching file and naming
- [x] `drv_attr_name` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite exports `/sys/.../driver/name` and smoke depends on it
- [x] `drv_attr_unbind` (static)
  - Linux Target: `linux2.6/drivers/base/bus.c::driver_attr_unbind`
  - Action: moved to Linux-matching file and naming

### `drivers/base/platform.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/base/platform.c`
- Plan: audit false positive; `platform_bus` / `platform_bus_type` already match Linux symbol naming and file placement
Checklist:
- [x] `platform_bus` (global)
  - Linux Target: `linux2.6/drivers/base/platform.c::platform_bus`
  - Action: no code move; remove from pending cleanup list
- [x] `platform_bus_type` (global)
  - Linux Target: `linux2.6/drivers/base/platform.c::platform_bus_type`
  - Action: no code move; remove from pending cleanup list

### `drivers/block/brd.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/block/brd.c`
- Plan: replace fixed per-disk globals with Linux-style per-device `brd_device` objects linked on `brd_devices`
Checklist:
- [x] `ramdisk0_disk` (global)
  - Linux Target: replaced by `linux2.6/drivers/block/brd.c::brd_devices` + per-device `brd_disk`
  - Action: removed fixed global disk object
- [x] `ramdisk1_disk` (static)
  - Linux Target: replaced by `linux2.6/drivers/block/brd.c::brd_devices` + per-device `brd_disk`
  - Action: removed fixed static disk object

### `drivers/input/serio/i8042.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/input/serio/i8042.c`
- Plan: replace Lite single-port globals with Linux-style `i8042_ports[]` + `i8042_platform_device`
Checklist:
- [x] `i8042_initialized` (static)
  - Linux Target: `linux2.6/drivers/input/serio/i8042.c` (no direct symbol)
  - Action: removed; use `i8042_ports[I8042_KBD_PORT_NO].serio != NULL` as presence state
- [x] `i8042_pdev` (static)
  - Linux Target: `linux2.6/drivers/input/serio/i8042.c::i8042_platform_device`
  - Action: renamed/aligned to `i8042_platform_device`
- [x] `i8042_port` (static)
  - Linux Target: `linux2.6/drivers/input/serio/i8042.c::i8042_ports`
  - Action: replaced by `i8042_ports[]` (Lite keeps 1-port subset)

### `drivers/input/serio/serio.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/input/serio/serio.c`
- Plan: align `serio_bus` to Linux global; align port numbering to Linux `serio_init_port()` local `serio_no`
Checklist:
- [x] `serio_bus` (global)
  - Linux Target: `linux2.6/drivers/input/serio/serio.c::serio_bus`
  - Action: audit false positive; already correct symbol name and placement
- [x] `serio_port_no` (static)
  - Linux Target: `linux2.6/drivers/input/serio/serio.c::serio_init_port() local static serio_no`
  - Action: removed; replaced by function-local static counter (`serio_no`)

### `drivers/pci/pci.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/pci/pci-driver.c`, `linux2.6/drivers/pci/pci-sysfs.c`, `linux2.6/drivers/pci/probe.c`
- Plan: split Lite PCI core to Linux file placement; keep minimal subset behavior
Checklist:
- [x] `pci_bus_type` (global)
  - Linux Target: `linux2.6/drivers/pci/pci-driver.c::pci_bus_type`
  - Action: moved from `drivers/pci/pci.c` -> `drivers/pci/pci-driver.c`
- [x] `pci_dev_groups` (global)
  - Linux Target: `linux2.6/drivers/pci/pci-sysfs.c::pci_dev_groups`
  - Action: moved from `drivers/pci/pci.c` -> `drivers/pci/pci-sysfs.c`
- [x] `pci_dev_type` (global)
  - Linux Target: `linux2.6/drivers/pci/pci-sysfs.c::pci_dev_type`
  - Action: moved from `drivers/pci/pci.c` -> `drivers/pci/pci-sysfs.c`
- [x] `pcibus_class` (global)
  - Linux Target: `linux2.6/drivers/pci/probe.c::pcibus_class`
  - Action: moved from `drivers/pci/pci.c` -> `drivers/pci/probe.c`
- [x] `pci_next_bus` (static)
  - Linux Target: `linux2.6/drivers/pci/probe.c` (no direct symbol)
  - Action: removed global; replaced by local `next_bus` counter during scan
- [x] `pci_bus_dev_type` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; required by Lite sysfs layout (`/sys/class/pci_bus/*/type`) and smoke coverage

### `drivers/scsi/scsi.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed (`linux2.6/drivers/scsi/hosts.c`, `linux2.6/drivers/scsi/sd.c`, scan logic local state)
- Plan: move host/disk numbering state out of `scsi.c`; convert scan tuning globals to non-global constants
Checklist:
- [x] `scsi_host_next` (static)
  - Linux Target: `linux2.6/drivers/scsi/hosts.c::scsi_host_next_hn`
  - Action: removed from `scsi.c`; host numbering moved to `drivers/scsi/hosts.c`
- [x] `scsi_disk_next` (static)
  - Linux Target: `linux2.6/drivers/scsi/sd.c` disk index allocation state
  - Action: removed from `scsi.c`; disk numbering moved into `drivers/scsi/sd.c::scsi_alloc_disk()`
- [x] `scsi_report_luns_initial` (static)
  - Linux Target: scan-local tuning state
  - Action: removed global; converted to non-global scan constant
- [x] `scsi_scan_tur_retries` (static)
  - Linux Target: scan-local tuning state
  - Action: removed global; converted to non-global scan constant
- [x] `scsi_sequential_scan_max_luns` (static)
  - Linux Target: scan-local tuning state
  - Action: removed global; converted to non-global scan constant

### `drivers/tty/serial/8250/8250_core.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: align platform-device naming to Linux; keep Lite-only tty glue state documented
Checklist:
- [x] `serial8250_pdev` (static)
  - Linux Target: `linux2.6/drivers/tty/serial/8250/8250_core.c::serial8250_isa_devs`
  - Action: renamed/aligned to `serial8250_isa_devs`
- [x] `tty_serial8250_driver` (global)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only tty glue object required by simplified `uart_register_driver(drv, tty_drv)` API

### `drivers/tty/serial/serial_core.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: keep Lite-only default console sink state documented
Checklist:
- [x] `uart_default_port` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only default console sink used by `uart_default_put_char()` and early 8250 wiring

### `drivers/tty/tty_io.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: audit Linux-matching `tty_drivers` and keep Lite simplified active-tty/ldisc/output state documented
Checklist:
- [x] `foreground_pid` (static)
  - Linux Target: Linux foreground process-group/session state spread over `tty_struct`
  - Action: keep; Lite-only foreground pid cache
- [x] `tty_active` (static)
  - Linux Target: Linux current tty/fg console state spread over `tty_struct` and vt layer
  - Action: keep; Lite active tty pointer
- [x] `tty_drivers` (static)
  - Linux Target: `linux2.6/drivers/tty/tty_io.c::tty_drivers`
  - Action: audit false positive; already aligned by symbol and file
- [x] `tty_ldisc` (static)
  - Linux Target: Linux ldisc state handled across `tty_io.c` and `tty_ldisc.c`
  - Action: keep; Lite single active line-discipline pointer
- [x] `tty_output_targets` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only console output target bitmask
- [x] `user_exit_hook` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only user exit callback hook

### `drivers/virtio/virtio.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/drivers/virtio/virtio.c::virtio_index_ida`
- Plan: align global naming to Linux while keeping integer-only subset
Checklist:
- [x] `virtio_index` (static)
  - Linux Target: `linux2.6/drivers/virtio/virtio.c::virtio_index_ida`
  - Action: renamed/aligned to `virtio_index_ida` (Lite keeps integer-only subset, no full IDA)

### `fs/block_dev.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite block I/O telemetry documented
Checklist:
- [x] `blk_bytes_read` (static)
  - Linux Target: Linux block read/diskstats accounting
  - Action: keep; Lite bytes-read telemetry counter
- [x] `blk_bytes_written` (static)
  - Linux Target: Linux block write/diskstats accounting
  - Action: keep; Lite bytes-written telemetry counter
- [x] `blk_reads` (static)
  - Linux Target: Linux block read accounting
  - Action: keep; Lite read request telemetry counter
- [x] `blk_writes` (static)
  - Linux Target: Linux block write accounting
  - Action: keep; Lite write request telemetry counter

### `fs/buffer.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: remove macro false positive and keep Lite global buffer-head tracking list documented
Checklist:
- [x] `BH_HASH_SIZE` (static)
  - Linux Target: not applicable
  - Action: audit false positive; preprocessor macro, not a static symbol
- [x] `bh_all_head` (static)
  - Linux Target: Linux buffer_head global tracking / per-cpu LRU internals
  - Action: keep; Lite global buffer-head list head
- [x] `bh_all_tail` (static)
  - Linux Target: Linux buffer_head global tracking / per-cpu LRU internals
  - Action: keep; Lite global buffer-head list tail
- [x] `bh_total` (static)
  - Linux Target: Linux buffer_head accounting such as `buffer_heads_over_limit`
  - Action: keep; Lite total buffer-head counter

### `fs/dcache.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: keep Lite explicit root dentry pointer documented
Checklist:
- [x] `vfs_root_dentry` (global)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only global root dentry anchor used by simplified VFS path resolution

### `fs/inode.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/fs/inode.c::get_next_ino` internal allocation state
- Plan: keep Lite global inode counter documented
Checklist:
- [x] `last_ino` (static)
  - Linux Target: `linux2.6/fs/inode.c::get_next_ino` backing state
  - Action: keep; Lite-only single global inode counter

### `fs/namespace.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): NO_DIRECT_LINUX_MATCH
- Plan: keep Lite mount list head documented
Checklist:
- [x] `vfs_mounts` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only linear mount list head for simplified namespace model

### `fs/proc/base.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): not applicable
- Plan: remove macro false positive
Checklist:
- [x] `PROC_PID_MAX` (static)
  - Linux Target: not applicable
  - Action: audit false positive; preprocessor constant, not a static symbol

### `fs/proc/generic.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite fixed `/proc` entry inode set documented; Linux has equivalent proc nodes but not as these static per-file globals
Checklist:
- [x] `proc_blockstats` (static)
  - Linux Target: `linux2.6/fs/proc` equivalent proc entry set
  - Action: keep; Lite fixed `/proc/blockstats` inode object
- [x] `proc_cow` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite fixed `/proc/cow` inode object
- [x] `proc_diskstats` (static)
  - Linux Target: `linux2.6/fs/proc/diskstats.c`
  - Action: keep; Lite fixed `/proc/diskstats` inode object
- [x] `proc_iomem` (static)
  - Linux Target: `linux2.6/fs/proc/iomem.c`
  - Action: keep; Lite fixed `/proc/iomem` inode object
- [x] `proc_maps` (static)
  - Linux Target: `linux2.6/fs/proc/task_mmu.c`
  - Action: keep; Lite fixed `/proc/maps` inode object
- [x] `proc_mounts` (static)
  - Linux Target: `linux2.6/fs/proc_namespace.c`
  - Action: keep; Lite fixed `/proc/mounts` inode object
- [x] `proc_pagecache` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite fixed `/proc/pagecache` inode object
- [x] `proc_pfault` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite fixed `/proc/pfault` inode object
- [x] `proc_sched` (static)
  - Linux Target: `linux2.6/fs/proc/base.c` scheduling-related proc output
  - Action: keep; Lite fixed `/proc/sched` inode object
- [x] `proc_vmscan` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite fixed `/proc/vmscan` inode object
- [x] `proc_writeback` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite fixed `/proc/writeback` inode object

### `fs/proc/root.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite minimal procfs root metadata documented; Linux uses richer `proc_dir_entry` tree infrastructure
Checklist:
- [x] `proc_dirent` (static)
  - Linux Target: `linux2.6/fs/proc/root.c` proc root dir entry infrastructure
  - Action: keep; Lite fixed proc root dirent object
- [x] `proc_root_children` (global)
  - Linux Target: `linux2.6/fs/proc/root.c` child linkage within proc tree
  - Action: keep; Lite linear child list head for simplified proc root
- [x] `proc_root_children_nr` (static)
  - Linux Target: `linux2.6/fs/proc/root.c` child accounting within proc tree
  - Action: keep; Lite child count for simplified proc root
- [x] `procfs_dir_iops` (static)
  - Linux Target: `linux2.6/fs/proc/root.c::proc_dir_inode_operations`
  - Action: keep; Lite inode ops object for procfs directory root

### `fs/ramfs/inode.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: align obvious false positive and keep Lite ramfs ops subset documented
Checklist:
- [x] `ramfs_dir_iops` (static)
  - Linux Target: `linux2.6/fs/ramfs/inode.c::ramfs_dir_inode_operations`
  - Action: keep; Lite ramfs directory inode ops subset
- [x] `ramfs_dir_ops` (static)
  - Linux Target: `linux2.6/fs/ramfs/inode.c::simple_dir_operations`
  - Action: keep; Lite local dir file-ops wrapper around generic readdir subset
- [x] `ramfs_file_ops` (static)
  - Linux Target: `linux2.6/fs/ramfs/inode.c::ramfs_file_operations`
  - Action: keep; Lite ramfs file ops subset
- [x] `ramfs_symlink_ops` (static)
  - Linux Target: `linux2.6/fs/ramfs/inode.c::page_symlink_inode_operations`
  - Action: keep; Lite symlink file-ops subset due simplified VFS model
- [x] `x52414D46` (global)
  - Linux Target: `linux2.6/fs/ramfs/inode.c::RAMFS_MAGIC`
  - Action: audit false positive; hexadecimal magic constant / enum value, not a global symbol

### `fs/sysfs/dir.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite pre-kernfs sysfs node/ops tables documented; Linux equivalents moved into kernfs/sysfs split and are not 1:1 globals
Checklist:
- [x] `sys_bus_devices_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent directory inode ops
  - Action: keep; Lite sysfs bus/devices inode ops object
- [x] `sys_bus_devices_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; Lite sysfs bus/devices file ops object
- [x] `sys_bus_devices_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; duplicate audit entry resolved by same Lite sysfs bus/devices file ops object
- [x] `sys_bus_dir_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent directory inode ops
  - Action: keep; Lite sysfs bus dir inode ops object
- [x] `sys_bus_dir_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; Lite sysfs bus dir file ops object
- [x] `sys_bus_entry_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent entry inode ops
  - Action: keep; Lite sysfs bus entry inode ops object
- [x] `sys_bus_entry_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; Lite sysfs bus entry file ops object
- [x] `sys_class_dir_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent class dir inode ops
  - Action: keep; Lite sysfs class dir inode ops object
- [x] `sys_class_dir_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; Lite sysfs class dir file ops object
- [x] `sys_class_root_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent class root inode ops
  - Action: keep; Lite sysfs class root inode ops object
- [x] `sys_class_root_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; Lite sysfs class root file ops object
- [x] `sys_dead_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs/dir.c` dead-node behavior via sysfs/kernfs state
  - Action: keep; Lite dead sysfs node ops object
- [x] `sys_devices_dir_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent devices dir inode ops
  - Action: keep; Lite sysfs devices dir inode ops object
- [x] `sys_devices_dir_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent file ops
  - Action: keep; Lite sysfs devices dir file ops object
- [x] `sys_dir_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent generic dir inode ops
  - Action: keep; Lite generic sysfs dir inode ops object
- [x] `sys_dir_ops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent generic file ops
  - Action: keep; Lite generic sysfs dir file ops object
- [x] `sys_dirent` (global)
  - Linux Target: `linux2.6/fs/sysfs/dir.c` sysfs dirent / kernfs node root structures
  - Action: keep; Lite root sysfs dirent object
- [x] `sys_kobj_dir_iops` (global)
  - Linux Target: `linux2.6/fs/sysfs` / `fs/kernfs` equivalent kobject dir inode ops
  - Action: keep; Lite kobject sysfs dir inode ops object
- [x] `sysfs_next_ino` (static)
  - Linux Target: `linux2.6/fs/sysfs/dir.c` inode/id allocation state
  - Action: keep; Lite sysfs inode counter for simplified sysfs namespace
- [x] `sysfs_root_dentry` (global)
  - Linux Target: `linux2.6/fs/sysfs/mount.c` root dentry / mount anchor split
  - Action: keep; Lite explicit sysfs root dentry anchor

### `init/main.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep fixed buffers as Lite subset backing Linux-named pointers `saved_command_line` and `execute_command`
Checklist:
- [x] `execute_command_buf` (static)
  - Linux Target: `linux2.6/init/main.c::execute_command`
  - Action: keep; fixed-size backing storage for Linux-named pointer `execute_command`
- [x] `saved_command_line_buf` (static)
  - Linux Target: `linux2.6/init/main.c::saved_command_line`
  - Action: keep; fixed-size backing storage for Linux-named pointer `saved_command_line`

### `kernel/ksysfs.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/kernel/ksysfs.c`
- Plan: audit false positive; `kernel_kobj` already matches Linux symbol and file placement
Checklist:
- [x] `kernel_kobj` (global)
  - Linux Target: `linux2.6/kernel/ksysfs.c::kernel_kobj`
  - Action: no code move; remove from pending cleanup list

### `kernel/kthread.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/kernel/kthread.c`
- Plan: audit false positive; `kthread_create_list` already matches Linux symbol and file placement, Lite only simplifies list representation
Checklist:
- [x] `kthread_create_list` (global)
  - Linux Target: `linux2.6/kernel/kthread.c::kthread_create_list`
  - Action: no code move; remove from pending cleanup list

### `kernel/printk/printk.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: align console list naming to Linux and keep Lite ring-buffer counters documented
Checklist:
- [x] `console_list` (static)
  - Linux Target: `linux2.6/kernel/printk/printk.c::console_drivers`
  - Action: renamed/aligned to Linux naming `console_drivers`
- [x] `printk_log_count` (static)
  - Linux Target: `linux2.6/kernel/printk/printk.c::logged_chars` / log buffer state
  - Action: keep; Lite ring-buffer count state
- [x] `printk_log_head` (static)
  - Linux Target: `linux2.6/kernel/printk/printk.c::log_end` / log buffer head state
  - Action: keep; Lite ring-buffer head state

### `kernel/sched/core.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep exported compatibility mirrors and Lite-only scheduler counters documented
Checklist:
- [x] `current` (global)
  - Linux Target: Linux `current` current-task surface
  - Action: keep; Lite exported compatibility mirror of `boot_cpu_sched.current`
- [x] `last_pid` (global)
  - Linux Target: Linux PID allocation backing state
  - Action: keep; Lite single global PID allocator counter used by `fork.c`
- [x] `need_resched` (global)
  - Linux Target: Linux need-resched thread flag surface
  - Action: keep; Lite exported compatibility mirror of scheduler resched state
- [x] `sched_switch_count` (global)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only scheduler switch telemetry counter

### `kernel/time/clockevents.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/kernel/time/tick-common.c::tick_cpu_device`
- Plan: keep Lite single-CPU subset documented; no full `struct tick_device` / per-CPU infrastructure yet
Checklist:
- [x] `tick_device` (static)
  - Linux Target: `linux2.6/kernel/time/tick-common.c::tick_cpu_device`
  - Action: keep; Lite-only single-CPU current tick device pointer

### `kernel/time/time.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/kernel/time/jiffies.c`
- Plan: move global tick counter to Linux-matching file; keep accessors in `time.c`
Checklist:
- [x] `jiffies` (global)
  - Linux Target: `linux2.6/kernel/time/jiffies.c::jiffies`
  - Action: moved from `kernel/time/time.c` to `kernel/time/jiffies.c`

### `lib/kobject_uevent.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): none
- Plan: audit false positive; `UEVENT_HELPER_PATH_LEN` is a macro in `include/linux/kobject.h`, not a global variable in `lib/kobject_uevent.c`
Checklist:
- [x] `UEVENT_HELPER_PATH_LEN` (global)
  - Linux Target: not applicable
  - Action: remove from global-variable cleanup list; keep macro in `include/linux/kobject.h`

### `mm/bootmem.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite bootmem aggregate state documented; remove linker-symbol false positive
Checklist:
- [x] `bootmem` (static)
  - Linux Target: Linux bootmem allocator state across `linux2.6/mm/bootmem.c`
  - Action: keep; Lite aggregated bootmem/e820/module state container
- [x] `end` (global)
  - Linux Target: linker-provided kernel end symbol
  - Action: audit false positive; extern linker symbol, not a mutable bootmem global

### `mm/filemap.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite page-cache registry and hit/miss telemetry documented
Checklist:
- [x] `mapping_list` (global)
  - Linux Target: Linux address_space registry/list state across page cache internals
  - Action: keep; Lite linear address_space registry head
- [x] `pcache_hits` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite page-cache hit telemetry counter
- [x] `pcache_misses` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite page-cache miss telemetry counter

### `mm/memory.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite page-table root cache and page-fault/COW telemetry documented
Checklist:
- [x] `cow_copies` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite copy-on-write copy telemetry counter
- [x] `cow_faults` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite copy-on-write fault telemetry counter
- [x] `kernel_directory` (static)
  - Linux Target: Linux kernel page-table root state in mm/pgtable setup
  - Action: keep; Lite cached kernel page-directory root
- [x] `page_directory` (static)
  - Linux Target: Linux current mm pgd / CR3 state
  - Action: keep; Lite cached active page-directory root
- [x] `pf_kernel` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite page-fault kernel-mode telemetry counter
- [x] `pf_kernel_addr` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite page-fault kernel-address-range telemetry counter
- [x] `pf_not_present` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite non-present fault telemetry counter
- [x] `pf_null` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite null-address fault telemetry counter
- [x] `pf_out_of_range` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite out-of-range fault telemetry counter
- [x] `pf_present` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite protection/present fault telemetry counter
- [x] `pf_prot` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite protection fault telemetry counter
- [x] `pf_reserved` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite reserved-bit fault telemetry counter
- [x] `pf_total` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite total page-fault telemetry counter
- [x] `pf_user` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite page-fault user-mode telemetry counter
- [x] `pf_write` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only page-fault write telemetry counter

### `mm/mmzone.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite single-node zonelist/cache state documented
Checklist:
- [x] `contig_zonelist` (global)
  - Linux Target: Linux zonelist state in `linux2.6/mm/page_alloc.c`
  - Action: keep; Lite single-node contiguous zonelist cache
- [x] `dma_zonelist` (global)
  - Linux Target: Linux DMA zonelist state in `linux2.6/mm/page_alloc.c`
  - Action: keep; Lite single-node DMA zonelist cache
- [x] `mem_map_pages` (static)
  - Linux Target: Linux mem_map sizing/accounting state
  - Action: keep; Lite mem_map page-count cache

### `mm/page-writeback.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/mm/page-writeback.c` writeback accounting/throttling state
- Plan: keep Lite writeback counters documented
Checklist:
- [x] `wb_cleaned_pages` (static)
  - Linux Target: `linux2.6/mm/page-writeback.c` writeback completion/accounting state
  - Action: keep; Lite cleaned-page telemetry counter
- [x] `wb_dirty_pages` (static)
  - Linux Target: `linux2.6/mm/page-writeback.c` dirty accounting state
  - Action: keep; Lite dirty-page telemetry counter
- [x] `wb_discarded_pages` (static)
  - Linux Target: `linux2.6/mm/page-writeback.c` discard/writeback accounting state
  - Action: keep; Lite discarded-page telemetry counter
- [x] `wb_throttled` (static)
  - Linux Target: `linux2.6/mm/page-writeback.c` throttling state
  - Action: keep; Lite throttling event counter

### `mm/page_alloc.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: keep Lite buddy/bootmem allocator state documented; Linux has richer page allocator state but not these exact globals
Checklist:
- [x] `buddy_max_order` (static)
  - Linux Target: Linux buddy allocator order state in `linux2.6/mm/page_alloc.c`
  - Action: keep; Lite-only global max order for simplified buddy allocator
- [x] `buddy_next` (static)
  - Linux Target: Linux free list linkage embedded in allocator metadata
  - Action: keep; Lite-only buddy linked-list backing array
- [x] `buddy_ready` (static)
  - Linux Target: Linux boot/runtime allocator phase split
  - Action: keep; Lite bootmem-to-buddy readiness latch
- [x] `cached_mbi` (static)
  - Linux Target: NO_DIRECT_LINUX_MATCH
  - Action: keep; Lite-only Multiboot handoff cache for page allocator setup
- [x] `managed_pages_total` (static)
  - Linux Target: `linux2.6/mm/page_alloc.c::nr_kernel_pages` / managed page accounting
  - Action: keep; Lite aggregate managed page counter
- [x] `total_memory_kb` (static)
  - Linux Target: Linux boot memory sizing state
  - Action: keep; Lite-only cached physical memory size from Multiboot
- [x] `total_pages` (static)
  - Linux Target: `linux2.6/mm/page_alloc.c::nr_all_pages`
  - Action: keep; Lite total page count cache

### `mm/slab.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): mixed
- Plan: remove macro false positives and keep Lite slab metadata pool documented
Checklist:
- [x] `SLAB_MAX_CACHE` (global)
  - Linux Target: not applicable
  - Action: audit false positive; preprocessor macro, not a global symbol
- [x] `SLAB_MAX_CACHE` (static)
  - Linux Target: not applicable
  - Action: audit false positive; preprocessor macro, not a static symbol
- [x] `SLAB_MAX_PAGES` (static)
  - Linux Target: not applicable
  - Action: audit false positive; preprocessor macro, not a static symbol
- [x] `SLAB_MAX_PAGES` (static)
  - Linux Target: not applicable
  - Action: audit false positive; duplicate macro hit
- [x] `slab_free_list` (static)
  - Linux Target: Linux slab metadata free lists in allocator internals
  - Action: keep; Lite slab metadata freelist
- [x] `slab_pages` (static)
  - Linux Target: Linux slab page accounting internals
  - Action: keep; Lite slab page count for fixed metadata map

### `mm/swap.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): not applicable
- Plan: remove macro false positive
Checklist:
- [x] `SWAP_SLOTS` (global)
  - Linux Target: not applicable
  - Action: audit false positive; preprocessor constant, not a global symbol

### `mm/vmalloc.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/mm/vmalloc.c` vmalloc window bounds / `vmap_area_root`
- Plan: keep Lite cached vmalloc window bounds documented
Checklist:
- [x] `vmalloc_base` (static)
  - Linux Target: `linux2.6/mm/vmalloc.c` vmalloc window lower bound handling
  - Action: keep; Lite cached vmalloc base address
- [x] `vmalloc_end` (static)
  - Linux Target: `linux2.6/mm/vmalloc.c` vmalloc window upper bound handling
  - Action: keep; Lite cached vmalloc end address

### `mm/vmscan.c`
- Status: [x]
- Count: 0 pending
- Linux Target(s): `linux2.6/mm/vmscan.c` kswapd reclaim/scan state
- Plan: keep Lite kswapd counters documented
Checklist:
- [x] `kswapd_anon_reclaims` (static)
  - Linux Target: `linux2.6/mm/vmscan.c` anonymous reclaim accounting
  - Action: keep; Lite anonymous reclaim telemetry counter
- [x] `kswapd_file_reclaims` (static)
  - Linux Target: `linux2.6/mm/vmscan.c` file reclaim accounting
  - Action: keep; Lite file reclaim telemetry counter
- [x] `kswapd_reclaims` (static)
  - Linux Target: `linux2.6/mm/vmscan.c` reclaim accounting
  - Action: keep; Lite total reclaim telemetry counter
- [x] `kswapd_running` (static)
  - Linux Target: `linux2.6/mm/vmscan.c` kswapd lifecycle state
  - Action: keep; Lite kswapd running latch
- [x] `kswapd_tries` (static)
  - Linux Target: `linux2.6/mm/vmscan.c` scan/reclaim progress accounting
  - Action: keep; Lite kswapd scan-attempt counter
- [x] `kswapd_wakeups` (static)
  - Linux Target: `linux2.6/mm/vmscan.c` wakeup accounting
  - Action: keep; Lite kswapd wakeup counter
