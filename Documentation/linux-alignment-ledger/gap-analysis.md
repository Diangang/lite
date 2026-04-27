# Lite vs Linux 2.6 差距参考账本

> 本文件是 **Linux 对齐参考账本**，记录仅限当前 Lite 已实现功能范围下，相对 `linux2.6/` 的重要差距点与收敛顺序。
> 范围约束：**不新增额外功能**；基础设施/基础框架必须靠拢 Linux 2.6。
> 权威参照：`linux2.6/`（本仓内 vendored 副本）。

## 文档定位

- 性质：账本 / 规划参考（非执行日志）。
- 对应流程：遵循 `.trae/skills/linux-alignment/SKILL.md` 的 Reference-First、Same-Symbol-Same-File、Simplify-by-Subsetting 原则。
- 关联文档：
  - [Linux26-Subsystem-Alignment.md](file:///data25/lidg/lite/Documentation/Linux26-Subsystem-Alignment.md)：阶段进度真相源
  - [global-vars-alignment-plan.md](file:///data25/lidg/lite/Documentation/linux-alignment-ledger/global-vars-alignment-plan.md)：全局变量 Phase 1（已清零）
  - [placement-diff-plan.md](file:///data25/lidg/lite/Documentation/linux-alignment-ledger/placement-diff-plan.md)：38 项文件落位 DIFF（已闭环）
  - [linux26_struct_audit.md](file:///data25/lidg/lite/Documentation/linux26_struct_audit.md)：结构体命名审计
  - [linux26_alignment_matrix.md](file:///data25/lidg/lite/Documentation/linux26_alignment_matrix.md)：目录级对齐矩阵
  - [README.md](file:///data25/lidg/lite/Documentation/linux-alignment-ledger/README.md)：自动生成的汇总账本

## 当前治理快照

| 维度 | 数值 | 说明 |
|---|---:|---|
| Lite 总源文件 | 207 | arch/block/drivers/fs/include/init/kernel/lib/mm |
| `NO_DIRECT_LINUX_MATCH` 函数 | 839 | 含合理 Lite 子集与可收敛残余 |
| `NO_DIRECT_LINUX_MATCH` 结构体 | 31 | 多为 Lite 简化模型 |
| `NO_DIRECT_LINUX_MATCH` 全局变量 | 198 | Phase 1 已分类完毕（Lite-only 子集） |
| 初始化调用名称未匹配 | 1 | 仅剩 `pci_init`（`genhd_device_init` 已确认在 `block/genhd.c` 与 Linux 同文件同级别；`pcie_init` 账本项已确认是过期误报并清理；virtio_pci_init, serio_core_init, serial8250_driver_init 已对齐；`scsi_init_hosts`/`scsi_sysfs_register` 已收敛到 `init_scsi` 主流程；`sd_disk_class_init` 已对齐为 `init_sd`；`tty_class_init`/`pcibus_class_init` 已对齐到 `postcore_initcall`；`virtio_init` 已对齐到 `core_initcall`；`nvme_class_init` 已收敛到 `nvme_init` 主流程） |
| Placement DIFF 文件 | 0（原 38 已全部闭环） |

## 重要差距分级（按影响力排序）

### P0 — 运行正确性 / SMP 前景

#### P0-1 调度器 per-CPU 基础设施
- Linux 参照：`linux2.6/kernel/sched.c`（per-CPU `rq`、per-CPU `current`、`TIF_NEED_RESCHED`、reschedule IPI）
- Lite 现状：[kernel/sched/core.c](file:///data25/lidg/lite/kernel/sched/core.c)
  - 仅存在 `boot_cpu_sched` 单实例
  - `current` / `need_resched` 仍是导出全局"兼容镜像"
- 影响：fork/exit/wait/signal 生命周期仅在 UP 下成立；`tasklist_lock()` 实际为 `irq_save/restore`
- Consistency：Naming OK / Placement OK / Semantics PARTIAL DIFF / Flow DIFF
- Plan（不扩功能）：把 `boot_cpu_sched` 改写成 `runqueues[NR_CPUS]`（`NR_CPUS=1`）形状；调度决策继续走 `task_current()` / `task_need_resched()`

#### P0-2 task refcount / lifetime
- Linux 参照：`linux2.6/kernel/exit.c::release_task()`、`linux2.6/include/linux/sched.h::put_task_struct`
- Lite 现状：reap 后直接 `kfree(task)`；`task_release_invariant_holds()` 为 UP-only 静态断言
- 影响：并发 wait/kill/exit 观察者无法安全共存
- Plan：先引入 `get_task_struct` / `put_task_struct` 命名壳（Simplify by Subsetting），保留现行 UP 语义

#### P0-3 锁原语缺真实实现
- Linux 参照：`linux2.6/include/linux/spinlock.h`、`linux2.6/arch/x86/include/asm/atomic.h`
- Lite 现状：所有"锁"退化为 `irq_save/irq_restore`；`include/linux/spinlock.h`、`include/linux/atomic.h` 为 UP 最小壳
- 影响：tasklist/wait queue/kref 的锁顺序只是"形状对齐"
- Plan：保持 UP 壳命名，不破坏现有调用面；待 SMP 工作启动时再补真实原语

#### P0-4 irq 子系统（APIC / IOAPIC / IPI）
- Linux 参照：`linux2.6/arch/x86/kernel/apic/apic.c`、`linux2.6/arch/x86/kernel/apic/io_apic.c`、`linux2.6/arch/x86/kernel/smp.c`
- Lite 现状：[arch/x86/kernel/apic/apic.c](file:///data25/lidg/lite/arch/x86/kernel/apic/apic.c) 为 Stage 6-13 符号占位；LAPIC timer / IPI send / IOAPIC routing 全部 no-op
- 影响：APIC 向量被配为 panic handler；无法 SMP
- Plan：保持 arch-owned 边界不回退；以 Stage 14 继续 APIC-local 语义边界细分

### P1 — 基础设施直接影响上层语义

#### P1-5 MM buddy / zone / reclaim 真实语义
- Linux 参照：`linux2.6/mm/page_alloc.c`、`linux2.6/mm/vmscan.c`
- Lite 现状：[mm/page_alloc.c](file:///data25/lidg/lite/mm/page_alloc.c) buddy 数据结构已对齐（`free_area[MAX_ORDER]`）；但：
  - 无 migrate type
  - `alloc_build_scan_control` 仅 `may_*=1`
  - `kswapd` / `shrink_zone` 仅计数，不做真实 LRU 扫描/回写
- Plan：优先让 `scan_control` 字段名/顺序与 `linux2.6/mm/vmscan.c::scan_control` 对齐

#### P1-6 slab 实现模型
- Linux 参照：`linux2.6/mm/slab.c`
- Lite 现状：[mm/slab.c](file:///data25/lidg/lite/mm/slab.c)
  - 单链 slab + 固定 `cache_sizes[9]` + 静态 `slab_pool[4096]`
  - 无 per-cache 三链（full/partial/free），无 per-CPU `array_cache`
- Plan：在不改实现算法的前提下，为 `struct kmem_cache` 补 Linux 字段名（`name` / `objsize` / `flags` / `nodelists`），保留单链实现为 Linux 子集

#### P1-7 Page Cache / Writeback
- Linux 参照：`linux2.6/mm/filemap.c`、`linux2.6/mm/page-writeback.c`
- Lite 现状：[mm/filemap.c](file:///data25/lidg/lite/mm/filemap.c) 用 `struct page_cache_entry` 链表挂 `address_space`；`balance_dirty_pages_lite` 为简化名
- Plan：把 `balance_dirty_pages_lite` 改名 `balance_dirty_pages`（保持 Lite 子集语义），消除 `*_lite` 后缀 NO_MATCH

#### P1-8 idr 底层仍是平坦数组
- Linux 参照：`linux2.6/lib/idr.c`（radix-tree layered allocator）
- Lite 现状：[lib/idr.c](file:///data25/lidg/lite/lib/idr.c) 扁平可增长数组；`lib/radix-tree.c` 已引入但 idr 未使用
- Plan：先补齐 radix-tree public API；暂不切换 idr 后端（保持 API-only 对齐）

### P2 — 设备模型 / VFS 语义 Gap

#### P2-9 kobject / kset / sysfs kernfs 状态机
- Linux 参照：`linux2.6/fs/sysfs/dir.c`
- Lite 现状：[fs/sysfs/sysfs.h](file:///data25/lidg/lite/fs/sysfs/sysfs.h) 只有 `sysfs_dirent` 子集，缺状态标志 / ns
- Plan：不新增 kernfs；仅把 `sysfs_dirent` 字段命名向 Linux 靠拢（`s_parent`/`s_sibling`/`s_children`）

#### P2-10 VFS lookup / path walk
- Linux 参照：`linux2.6/fs/namei.c`（`struct nameidata` + `path_walk` + `LOOKUP_*` flags）
- Lite 现状：[fs/namei.c](file:///data25/lidg/lite/fs/namei.c) 使用 `vfs_build_abs` 字符串拼接
- Plan：先引入 `struct nameidata` 壳（subset），在调用边界做转换；不改查找算法

#### P2-11 Dcache 哈希与引用计数
- Linux 参照：`linux2.6/fs/dcache.c`（`dcache_hashtable` + `lru_list` + `DCACHE_*` flags）
- Lite 现状：[fs/dcache.c](file:///data25/lidg/lite/fs/dcache.c) 引入 `dcache_hash`（NO_MATCH）
- Plan：把 `dcache_hash` 改名 `dentry_hashtable`；散列函数贴近 `full_name_hash`

#### P2-12 buffer_head 哈希 / LRU
- Linux 参照：`linux2.6/fs/buffer.c`
- Lite 现状：`bh_all_head/tail` + 简化 hash
- Plan：把 `bh_all_head/tail/total` 统一为 `list_head` 模型并命名对齐 Linux

#### P2-13 block layer bio / request 真实调度
- Linux 参照：`linux2.6/block/`
- Lite 现状：[block/blk-core.c](file:///data25/lidg/lite/block/blk-core.c)
  - `bio_endio_sync` 为同步简化
  - 无 merge / sort / elevator
- Plan：保留同步路径；补 `elv_*` 占位符号（no-op）以对齐符号图

### P3 — 驱动 / 可观测 ABI 次要偏差

#### P3-14 driver core deferred probe
- Linux 参照：`linux2.6/drivers/base/dd.c`
- Lite 现状：`deferred_devs[]` 定长数组
- Plan：改为 `list_head deferred_probe_list`，保持单线程同步执行语义

#### P3-15 tty / n_tty / console 绑定
- Linux 参照：`linux2.6/drivers/tty/tty_io.c`
- Lite 现状：[drivers/tty/tty_io.c](file:///data25/lidg/lite/drivers/tty/tty_io.c) 单 active tty、单 ldisc；`tty_output_targets` 位图是 Lite-only
- Plan：`tty_output_targets` 保留但打标 Lite-only；其它字段命名对齐

#### P3-16 printk ring buffer / level
- Linux 参照：`linux2.6/kernel/printk.c`
- Lite 现状：`console_list` 简化链表，无 ring buffer / `KERN_*` level
- Plan：先补 `KERN_*` 常量与前缀解析（no-op 消费），不引入真实 ring buffer

#### P3-17 初始化调用名称
- 仍有若干 `subsys_initcall` / `module_init` 名称或挂接方式未完全对上 Linux
- 参照：[initcalls.lite.csv](file:///data25/lidg/lite/Documentation/linux-alignment-ledger/initcalls.lite.csv)
- Plan：优先收敛“同文件、纯命名”差异；对于 Linux 中不应独立注册的入口，改为收敛到 Linux 主初始化流程（如 `init_scsi()`）

### P4 — 头文件 / 声明落位残余

#### P4-18 asm 头分层
- Lite `include/asm/*` 平铺；Linux 2.6 为 `arch/<arch>/include/asm/` + `include/asm-generic/`
- Plan：保持现状；记为 NO_DIRECT_LINUX_MATCH（目录层级差异），`asm-generic/` 按需点状新增

#### P4-19 Struct NO_DIRECT_LINUX_MATCH（31 项）
- 已完成：
  - `gdt_entry` → `desc_struct`
  - `idt_entry` → `gate_desc`
  - `idt_ptr` → `desc_ptr`
  - `tss_entry` → `x86_hw_tss`
- 保留并标注 Lite-only：
  - `bdev_map_entry` / `disk_map_entry`（Lite 的 gendisk 查找缓存）
  - `sched_cpu_state`（per-CPU 壳，见 P0-1）
  - `bootmem_region` / `bootmem_e820_entry` / `bootmem_module_range` / `bootmem_data`
  - `ramdisk_backend` / `req`（devtmpfs）
  - `slab` / `large_hdr` / `kmem_cache`（Lite 简化 slab 内部）
  - `swap_slot` / `vmalloc_block`
  - `sysfs_dirent` / `sysfs_named_inode`
  - `proc_seq_state` / `dcache_hash`（见 P2-10/11）
  - `minix_inode_disk` / `minix_mount_data`（minixfs 子集）
  - `virtqueue_buf` / `cpio_newc_header` / `multiboot_*`
  - `page_cache_entry`（见 P1-7）
  - `kthread_create_info` / `vsn_ctx` / `child_name_match`

## 推荐收敛顺序（不新增功能）

| # | 子项 | 动作类型 | 直接消除 NO_MATCH |
|---|---|---|---:|
| 1 | `gdt_entry/idt_entry/idt_ptr` → `desc_struct/gate_desc/desc_ptr`，落位 `arch/x86/include/asm/desc.h` | 命名 + 落位 | 3 条 struct |
| 2 | initcall 命名对齐（13 条） | 命名 | 13 条 initcall |
| 3 | slab 字段命名对齐（`name`/`objsize`/`flags`/`nodelists`） | 字段命名 | 间接减少 slab-系函数 NO_MATCH |
| 4 | `dcache_hash` → `dentry_hashtable`；散列函数贴近 `full_name_hash` | 命名 + 算法 | 1 条 struct + 相关函数 |
| 5 | `bh_all_*` → `list_head` 模型 + Linux 命名 | 数据结构 + 命名 | 4 条 global |
| 6 | `boot_cpu_sched` → `runqueues[NR_CPUS]`（UP=1） | 形状对齐 | 1 条 struct + 为 P0-1 铺路 |
| 7 | 最后一批 fs/drivers 对 `current`/`need_resched` 的直接读收敛到 `task_current()` | 调用点 | 允许最终移除兼容镜像 |
| 8 | `balance_dirty_pages_lite` → `balance_dirty_pages` | 命名 | 1 条 function |
| 9 | driver core deferred_probe：定长数组 → `list_head deferred_probe_list` | 数据结构 | 2 条 global |
| 10 | 把 839 条 `NO_DIRECT_LINUX_MATCH` 函数按子系统跑脚本分类：可重命名 / Lite-only helper / 真正残余 | 分类 | 缩减残余面 |

## 每轮验证流程

```sh
make clean && make -j4 && make smoke-512
```

接受标准（硬约束）：
- 任何一轮不允许在未提交账本前开始编辑（Gate 0）
- 任何一轮只允许收敛一个差异点（Gate 2）
- 无 Linux 对应的新增项必须标 `NO_DIRECT_LINUX_MATCH` + `Why/Impact/Plan`（Gate 3）
- `make smoke-512` 通过

## 状态看板（逐项进度）

| ID | 描述 | 状态 |
|---|---|---|
| P0-1 | runqueue per-CPU 形状 | [ ] pending |
| P0-2 | task refcount 壳（get_/put_task_struct） | [ ] pending |
| P0-3 | spinlock/atomic 真实原语 | [-] 受 SMP 阻塞，维持 UP 壳 |
| P0-4 | APIC/IOAPIC/IPI 真实启用 | [-] 受 SMP 阻塞，维持符号壳 |
| P1-5 | scan_control 字段对齐 | [ ] pending |
| P1-6 | kmem_cache 字段命名对齐 | [ ] pending |
| P1-7 | balance_dirty_pages 去 _lite 后缀 | [x] done（已验证：Lite 实际函数名为 `balance_dirty_pages_ratelimited`，与 Linux `linux2.6/mm/page-writeback.c` 同名同文件，本条为过期描述） |
| P1-8 | radix-tree public API 补齐（不切 idr 后端） | [ ] pending |
| P2-9 | sysfs_dirent 字段命名靠拢 s_* | [ ] pending |
| P2-10 | struct nameidata 壳引入 | [ ] pending |
| P2-11 | dcache_hash → dentry_hashtable | [-] blocked（Lite 的 `dcache_hash` 为 per-parent 桶缓存，Linux `dentry_hashtable` 为全局 `hlist_bl_head *`，语义不同；盲目改名将制造 Linux 不存在的语义，违反 Gate 3。保留原名，标 NO_DIRECT_LINUX_MATCH，待未来实现全局 dentry_hashtable 时再收敛） |
| P2-12 | bh_all_* → list_head | [ ] pending |
| P2-13 | elv_* 占位符号 | [ ] pending |
| P3-14 | deferred_probe_list | [ ] pending |
| P3-15 | tty 字段命名对齐 | [ ] pending |
| P3-16 | KERN_* 常量补齐 | [ ] pending |
| P3-17 | initcall 名称/挂接方式对齐 | [~] 进行中（12 项完成：`genhd_device_init` 已确认对齐于 `block/genhd.c`，`pcie_init` 账本误报已清理，virtio_pci_init → virtio_pci_driver_init, serio_core_init → serio_init, serial8250_driver_init → serial8250_init, `scsi_init_hosts`/`scsi_sysfs_register` 已收敛到 `init_scsi` 主流程，`sd_disk_class_init` → `init_sd`，`tty_class_init`/`pcibus_class_init` → `postcore_initcall`，`virtio_init` → `core_initcall`，`nvme_class_init` 已收敛到 `nvme_init` 主流程） |
| P4-18 | asm-generic 目录留空点位 | [-] 维持现状 |
| P4-19 | desc_struct/gate_desc/desc_ptr 改名 | [x] done |

图例：`[ ] pending` / `[x] done` / `[-] blocked or intentionally deferred`

## 使用方式

1. 每轮挑选 **唯一一个** 未完成项，读取对应 `linux2.6/` 源码与 Lite 源码。
2. 补写 Ledger（functions / structs / globals / files / directories + Placement OK/DIFF）。
3. 只做该差异点的最小修改，禁止顺手优化。
4. `make clean && make -j4 && make smoke-512` 通过后，将本文件对应项改为 `[x] done`，并在 `Linux26-Subsystem-Alignment.md` 的对应 Stage 追加一条进度行。
5. 若遇到本文件未列出的新差距，先补充到本文件再开始编辑。
