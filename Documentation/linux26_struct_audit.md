# Lite `include/linux` Struct Audit (Target: Linux 2.6 Naming)


## 文档定位
- 这是一份**命名审计文档**，关注 public header 中 `struct` 名称与 Linux 2.6 的对应关系。
- 它主要回答“名字是否对齐”，**不直接代表运行时语义已完全一致**。
- 运行时行为与生命周期请结合 `QA.md`、`device_driver_model.md` 和源码阅读。

This document audits **all** `struct` definitions under `include/linux/*.h` in this tree, focusing on **name compatibility** with Linux 2.6. Where a name cannot reasonably match Linux 2.6 (because Linux simply does not have an equivalent or semantics differ fundamentally), the entry includes a short rationale and the intended mapping.

Scope: only `include/linux/**/*.h` public headers (47 structs in this tree).

## Fully Name-Compatible (Struct Tag Matches Linux 2.6)

- `struct vm_area_struct`, `struct mm_struct` ([mm.h](file:///data25/lidg/lite/include/linux/mm.h))
- `struct request_queue` ([blk_queue.h](file:///data25/lidg/lite/include/linux/blk_queue.h))
- `struct console` ([console.h](file:///data25/lidg/lite/include/linux/console.h))
- `struct gendisk`, `struct block_device` ([blkdev.h](file:///data25/lidg/lite/include/linux/blkdev.h))
- `struct rb_node`, `struct rb_root` ([rbtree.h](file:///data25/lidg/lite/include/linux/rbtree.h))
- `struct buffer_head` ([buffer_head.h](file:///data25/lidg/lite/include/linux/buffer_head.h))
- `struct idr` ([idr.h](file:///data25/lidg/lite/include/linux/idr.h))
- `struct bio` ([bio.h](file:///data25/lidg/lite/include/linux/bio.h))
- `struct file_operations`, `struct inode`, `struct dirent`, `struct dentry`, `struct file_system_type`, `struct super_block`, `struct vfsmount` ([fs.h](file:///data25/lidg/lite/include/linux/fs.h))
- `struct tty_driver` ([tty.h](file:///data25/lidg/lite/include/linux/tty.h))
- `struct task_struct` ([sched.h](file:///data25/lidg/lite/include/linux/sched.h))
- `struct list_head`, `struct hlist_head`, `struct hlist_node` ([list.h](file:///data25/lidg/lite/include/linux/list.h))
- `struct address_space_operations`, `struct address_space` ([pagemap.h](file:///data25/lidg/lite/include/linux/pagemap.h))
- `struct device`, `struct bus_type`, `struct device_driver`, `struct class` ([device.h](file:///data25/lidg/lite/include/linux/device.h))
- `struct pci_dev`, `struct pci_device_id`, `struct pci_driver` ([pci.h](file:///data25/lidg/lite/include/linux/pci.h))
- `struct platform_device_id`, `struct platform_device`, `struct platform_driver` ([platform_device.h](file:///data25/lidg/lite/include/linux/platform_device.h))
- `struct kobject`, `struct kobj_type`, `struct kset` ([kobject.h](file:///data25/lidg/lite/include/linux/kobject.h))
- `struct request` ([blk_request.h](file:///data25/lidg/lite/include/linux/blk_request.h))
- `struct page`, `struct free_area`, `struct zone`, `struct pglist_data`, `struct zonelist` ([mmzone.h](file:///data25/lidg/lite/include/linux/mmzone.h))
- `struct kref` ([kref.h](file:///data25/lidg/lite/include/linux/kref.h))
- `struct file` ([file.h](file:///data25/lidg/lite/include/linux/file.h))
- `struct files_struct`, `struct fdtable` ([fdtable.h](file:///data25/lidg/lite/include/linux/fdtable.h))
- `struct fs_struct` ([fs_struct.h](file:///data25/lidg/lite/include/linux/fs_struct.h))

## Name-Compatible But Semantics Diverge (Explained)

- `struct device` ([device.h](file:///data25/lidg/lite/include/linux/device.h))
  - Linux 2.6: `struct device` is bus-agnostic; bus-specific identity typically lives in e.g. `struct pci_dev` embedding a `struct device`.
  - Lite: 已把 PCI identity/config/resource 字段迁出 `struct device`，收敛到 `struct pci_dev`，设备模型本身只保留通用字段。
  - Remaining delta: 一些 Lite helper 仍以 `struct device *` 为入参，再内部转换到 `pci_dev`，而不是像 Linux 那样更严格地区分 bus-specific helper 接口。

- `struct pci_driver` ([pci.h](file:///data25/lidg/lite/include/linux/pci.h))
  - Linux 2.6: pci core matches `pci_device_id` and passes the matched `id` into `->probe()`.
  - Lite: 已实现 `pci_bus->match()` 直接遍历 `pci_device_id` 来做 match，并在 `->probe()` 中把匹配到的 `id` 传给驱动（命名与调用姿势对齐 Linux 2.6）。
  - 差异：Lite 内部通过 `magic` 字段把 `struct device_driver *` 安全地识别为 `struct pci_driver *`（避免引入更大范围的 driver core 重构）；PCI config/PCIe helper 已收紧为 `struct pci_dev *` 类型边界，更接近 Linux 2.6 的 bus-specific helper 习惯。

- `struct platform_driver` ([platform_device.h](file:///data25/lidg/lite/include/linux/platform_device.h))
  - Linux 2.6: platform core 通过 `platform_bus_type.match` 做匹配，`->probe(struct platform_device *)` 接收 pdev。
  - Lite: 已实现 `platform_bus->match()` 遍历 `platform_device_id`，并把 probe 入口收敛为 `platform_driver_register()`（命名/角色与 Linux 2.6 对齐）。
  - 差异：为了可靠识别 platform_device，Lite 用 `dev->driver_data == pdev && &pdev->dev == dev` 的一致性检查；并用 `magic` 字段识别 platform_driver。

- `struct device_driver` / `struct device_id` ([device.h](file:///data25/lidg/lite/include/linux/device.h))
  - Linux 2.6: `struct device_driver` 本身不携带 `id_table`，match 由 `bus_type->match()` 与 bus-specific driver (`pci_driver`/`platform_driver`/...) 决定。
  - Lite: 已移除 `struct device_id` 与 `device_driver.id_table`（避免把“非 Linux 的泛化 match”伪装成 Linux 结构）。
  - Mapping: 对比学习时，应以 bus-specific 的 `*_device_id` 作为对照（目前已落地 `pci_device_id`；platform 后续再补齐）。

- `struct page_cache_entry` ([pagemap.h](file:///data25/lidg/lite/include/linux/pagemap.h))
  - Linux 2.6 uses `struct page` for page cache pages and indexes them via radix tree/xarray-like structures (radix tree in 2.6).
  - Lite keeps a separate `struct page_cache_entry` list node to model file cache mapping without fully converging on Linux’s `struct page` + radix tree design.
  - Mapping: “page cache entry” here corresponds to the *pagecache index node*, not the physical memory `struct page`.

## Previously Not Linux-Named, Now Aligned

- `struct tty_device` was renamed to `struct tty_port` ([tty.h](file:///data25/lidg/lite/include/linux/tty.h))
  - Rationale: Linux 2.6 has `struct tty_port` as the per-line/port abstraction; Lite’s `tty_device` had closest intent (per TTY line instance).
  - Note: semantics are still a minimal subset; the name is now compatible.

- `struct fd_struct` was replaced by Linux naming `struct fdtable` and `struct files_struct` updated to contain `struct fdtable` ([fdtable.h](file:///data25/lidg/lite/include/linux/fdtable.h))
  - Rationale: Linux uses `struct fdtable` and `struct files_struct`; Lite previously had a per-fd entry named `fd_struct` which does not exist in Linux.
  - Note: Lite’s `fdtable` is a compact fixed-size design, not Linux’s dynamic fdtable implementation.

## Next Refactor Candidates (To Reach Deeper Linux 2.6 Structural Parity)

- Continue replacing remaining Lite convenience glue in PCI paths with more Linux-like helper names/semantics where worthwhile.
  - Goal: the type boundary is now largely correct; follow-up work is about API naming/behavior polish rather than structural mismatch.

- Convert remaining “platform-like” drivers to `platform_driver` + `platform_device_id` where appropriate (e.g. i8042/serial in x86 init).
  - Goal: make role boundaries explicit and align learning/compare with Linux 2.6 driver model entry points.

- Add `/sys/devices/virtual` anchor and place bus-less class devices under it (e.g. `ram0`, global `/dev/tty`).
  - Goal: align sysfs layout with Linux 2.6 so `/sys/class/*/*` symlinks resolve into `/sys/devices/virtual/...` for virtual devices.

- Ensure hardware-backed tty class devices (e.g. `ttyS0`) remain class devices with `bus == NULL`, while their real location in `/sys/devices` still follows the physical parent device tree.
  - Goal: avoid polluting `/sys/bus/platform/devices` with class-only nodes and keep the Linux distinction between bus devices and class devices.
