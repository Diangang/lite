# Lite OS → Linux 2.6（Core Focus Plan v2）

目的：在当前 M0–M5 已经“能闭环”的基础上，把实现从“最小可用”推进到“Linux 2.6 可对照的关键语义与结构”，重点落在 **块层/缓存/磁盘 FS（Minix）主链路**，并补齐 **设备驱动模型（PCI/PCIe/NVMe）** 以支持从 NVMe 块设备挂载 Minix。写回线程模型与 swap/anon reclaim 作为后置阶段。

## 1. 现状与 Linux 2.6 差异（只列主线）

### 1.1 已经对齐到“可闭环”的部分（现状）
- 启动期内存图与 lowmem：multiboot memory map → e820 等价物；direct map 按 lowmem_end 动态化；/proc/meminfo 与 /proc/iomem 可观测。
- MM 主链路：缺页、匿名页、COW、rmap 最小化；vmscan/file cache reclaim 最小闭环。
- VFS 内存路径：page cache + dirty/writeback 统计与触发闭环；blockdev 通过 page cache 实现读写与刷回。
- 最小块 FS：minixfs（最小子集）可挂载并读取文件（用于验证 block+cache 的读路径）。

### 1.2 与 Linux 2.6 的关键差异（优先级从高到低）
- 块层语义缺失：没有 bio/request/request_queue（也没有调度/合并/plug/unplug）；当前 block_device 是“内存拷贝 + 统计”，不具备 Linux 2.6 的 I/O 生命周期结构。
- buffer cache 缺失：没有 buffer_head/buffer.c；page cache 与“块粒度元数据/日志/间接块”之间缺少承载层。
- address_space ops 缺失：Linux 2.6 的 `a_ops->readpage/writepage/write_begin/write_end` 等没有落地，导致 FS 与 page cache 的责任边界不可对照。
- 真实磁盘 Minix 目标缺失：minixfs 目前更偏“演示镜像生成 + 只读读取”，缺少基于 buffer cache 的真实格式读写路径与一致性语义（create/overwrite/truncate/fsync）。
- 设备驱动模型与 NVMe 设备链路不足：PCI/PCIe/NVMe 的枚举、namespace 暴露、/dev 节点与 sysfs 可观测还未收敛到“可挂载 NVMe 上 Minix”的状态。
- 写回线程模型与 swap 作为后置：暂无 pdflush/kupdate 风格的后台 writeback 与 dirty 回压；swap cache/pageout/pagein 与 vmscan 的 anon reclaim 联动需要后续补齐。

## 2. 新 Roadmap（v2，按里程碑）

### N0：结构对齐与接口收敛（P0）【已完成】

目标：在不扩展功能范围的前提下，把主链路接口命名与模块边界尽量收敛到 Linux 2.6，降低后续实现块层/FS 时的“概念跳跃”。

子任务清单（可逐项完成）：
- [x] 引入 `struct address_space_operations`：至少包含 `readpage/writepage` 或 `write_begin/write_end`，并挂到 `struct address_space`。
- [x] 把 `generic_file_read/write` 的“缺页填充/回写”路径改为调用 `a_ops`（普通文件、块设备走不同的 ops），避免在通用路径里直接判断 `FS_BLOCKDEVICE`。
- [x] 拆清 blockdev vs regular file 的 VFS 入口：块设备 inode 的 `f_ops` 走块层（submit_bio/ll_rw_block），普通文件 inode 的 `f_ops` 走 `a_ops` + page cache。
- [x] 统一 block device 的 inode 创建与容量语义：`i_size` 与设备容量/扇区大小一致，禁止“写扩容”这类非块设备语义。
- [x] init/main.c 结构继续与 linux2.6/init/main.c 对齐：函数分段、排列、前置声明最小化，不改语义。

交付项：
- address_space 操作集（最小）：引入 `address_space_operations`，让 page cache 的读写回调（readpage/writepage 或 write_begin/write_end）成为 FS 的对接点。
- blockdev/VFS 职责边界：把“块设备 inode 的读写”与“文件 inode 的读写”拆成更 Linux-like 的路径（即 blockdev 走 block 层；普通文件走 a_ops/FS）。
- init/main.c 结构对照：函数排列与调用分层尽量与 linux2.6/init/main.c 可对照（不改语义，仅改组织）。

验收：
- 现有 smoke 全部通过；代码层面可以在 `fs/*` 中清晰找到 page cache 与 FS 的边界接口。

### N1：块层骨架（bio → request → completion）（P1）【部分完成】

目标：把当前的 block_device 从“内存 memcpy”升级成“可对照 Linux 2.6 的块层生命周期”，即使设备仍是 ramdisk，也要走 `submit_bio → make_request/request_fn → end_request` 的形态。

子任务清单（可逐项完成）：
- [x] 头文件与数据结构：新增 `include/linux/bio.h`、`include/linux/blk_types.h`（或同等组织），定义最小 `bio/request/request_queue`。
- [x] 扇区模型：统一 512-byte sector 概念，定义 `sector_t`，明确 `bio->bi_sector/bi_size` 的单位与对齐规则。
- [x] 提交入口：实现 `submit_bio()`（同步完成版本），并把 completion 回调抽象为 `bio_endio`。
- [x] request 生成与派发：实现 `make_request_fn`（先走 no-merge/no-sched），把 bio 挂到 queue 并执行 `request_fn`。
- [x] 设备端最小 request_fn：ramdisk 从 `request` 取出 bio 并完成 memcpy；后续 NVMe driver 复用同一接口完成命令提交与完成。
- [x] 错误路径（最小）：越界/未就绪设备返回错误，并能回传到 VFS（read/write 返回值语义一致）。
- [ ] per-disk 统计：实现 `/proc/diskstats`（优先）或把 `/proc/blockstats` 扩展为 per-disk（reads/writes/sectors/bytes）。【延后】
- [x] 将现有 `block_device_read/write` 迁移为“块层之上的 helper”，底层必须走 `submit_bio`（避免双路径）。

交付项：
- `struct bio`：最小支持读/写、sector、len、data 指针（先不做 sglist）。
- `struct request` + `request_queue`：把 bio 合并/排序先做成“可插拔的框架”（默认 noop）。
- completion 路径：支持同步完成（先实现），预留异步完成/中断驱动的钩子。
- 可观测：新增 /proc/diskstats 或 /proc/blockstats 扩展为 per-disk 指标（reads/writes/sectors/merges/avg queue depth 至少部分字段）。

验收：
- blockstats/diskstats 能反映 bio 提交次数与扇区统计；读写一致性 smoke 覆盖。
- 仍然支持 /dev/ram0 的 page cache 读写与 writeback。

### N2：buffer cache（buffer_head）与块读写原语（P2）【部分完成】

目标：补齐 Linux 2.6 的 buffer.c 语义“承载层”，让磁盘 FS 的元数据读写不必“自己做 block read”，而是走统一的 buffer cache。

子任务清单（可逐项完成）：
- [x] 引入 `struct buffer_head`：blocknr、b_size、b_data、dirty/uptodate、引用计数。
- [x] buffer cache 哈希：以 `(bdev, blocknr)` 为 key 的查找/插入/淘汰框架，先不做复杂 LRU，只要可复用与可回收。
- [x] `bread()`：读块并返回 uptodate 的 bh；底层读必须走 N1 的 `submit_bio`。
- [x] `mark_buffer_dirty()`/同步写：写路径标脏并提交（同步写先做通；后台写回留给 N5）。
- [x] 块大小与对齐：支持 `bdev->block_size`（至少 512/1024），Minix 的 1KB 块要能正确映射到扇区。
- [x] 与 page cache 的协同边界：明确“文件数据页”与“元数据块”使用的缓存层，避免 FS 自己直接读写 bdev。
- [x] 回收路径：在 vmscan 下允许回收 clean buffer（dirty buffer 先拒绝回收，或先同步写再回收）。

交付项：
- `struct buffer_head`：块号、映射状态、dirty/uptodate、引用计数。
- `bread/bwrite/ll_rw_block`（最小）：基于 bio/request_queue 读写块，并提供 uptodate/dirty 语义。
- page cache 与 buffer cache 的协同策略（最小）：文件数据可继续走 page cache；FS 元数据走 buffer cache；允许后续扩展成 page cache page 与多个 bh 的映射（buffered I/O）。

验收：
- 新增回归：随机读取若干 inode/dir block，不依赖 FS 私有 block 读函数。
- 在内存压力下，buffer cache 可以被回收且不破坏一致性。

### N3：Minix 真实格式读写（P3）【已完成】

目标：把“最小块 FS”从演示性质推进到“可对照 Linux 2.6 的真实磁盘格式读写”，并把 VFS→cache→block 主链路固定下来（为 NVMe 挂载做准备）。

子任务清单（可逐项完成）：
- [x] Minix 版本选择：优先 Minix V1（14-char name、16-bit zone），明确限制并在 mount 时校验 magic。
- [x] 去掉“运行时生成镜像”的默认行为：镜像由 initramfs 携带（只在明确测试模式下允许格式化/生成）。
- [x] superblock 解析与校验：块大小、imap/zmap、firstdatazone、max_size、state。
- [x] inode 读写：通过 buffer cache 读取 inode table block，并实现写回（i_size、i_zone[]、mtime 等最小字段）。
- [x] 目录 lookup：在 `lookup/finddir` 路径支持按 name 查找 inode（跨目录）。
- [x] 文件数据读写：直接块（direct zones）读写先打通；不做一次/二次间接块（超出限制时返回错误）。
- [x] 分配器：最小 inode/zone bitmap 分配与释放（先支持创建/删除少量文件，不要求碎片整理）。
- [x] truncate：支持截断到 0 与截断到更小尺寸（释放 zone bitmap），并保证重启后可读一致。
- [x] fsync/close 语义（同步版）：fsync/close 必须触发相关 bh 的同步写回，确保 reopen 可见。
- [x] smoke 回归：新增 Minix RW 用例（create→write→close→open→read，overwrite，append，truncate）。

交付项：
- Minix 磁盘格式（优先 V1 结构）：superblock、inode table、dir entry、regular file 读写（先做覆盖写/截断/追加的最小子集）。
- 基于 buffer cache：元数据与数据块均通过 `bread/ll_rw_block` 等统一块读写原语，避免 FS 私有 block read。
- VFS 语义最小对齐：lookup、权限/类型位、`..`、root inode 号、i_size；写入后 re-open/read 可见一致数据。
- 镜像输入方式：initramfs 携带一个 minix 镜像文件，启动后通过块设备后端挂载（避免运行时生成镜像掩盖问题）。

验收：
- smoke：挂载 minix 镜像，覆盖写/截断/追加后读回校验；跨目录 lookup 校验内容与大小。
- /proc/mounts、/proc/diskstats（或增强版 blockstats）、/proc/meminfo 一致可观测。

### N4：设备驱动模型完善（PCI/PCIe/NVMe → 块设备 → 挂载 Minix）（P4）【已完成】

目标：补齐 device model 的关键链路，让 NVMe 设备能够以“Linux 2.6 可对照”的方式出现为块设备节点（/dev/nvme0n1），并能挂载 N3 的 Minix 读写。

子任务清单（可逐项完成）：
- [x] PCI 枚举稳定化：bus/slot/function 遍历、header type、BAR 探测与资源大小计算、桥设备递归扫描（secondary bus）。
- [x] PCIe capability：解析 PCIe cap，确认设备类型/链路能力（先用于可观测与 sanity check）。
- [x] 中断路径最小化：优先 MSI-X，其次 MSI，再退回 legacy INTx（至少一种能稳定工作并可复现）。
- [x] MMIO 映射：BAR 映射必须走 ioremap/固定映射窗口，禁止用 directmap 误映射设备 MMIO。
- [x] NVMe controller bring-up：
  - [x] 读取 CAP/VS，设置 CC，等待 CSTS.RDY。
  - [x] admin queue 建立（ASQ/ACQ）与 doorbell；实现 Identify Controller/Namespace。
  - [x] I/O queue 建立（至少一个 SQ/CQ），实现 Read/Write 命令提交与完成。
- [x] Namespace → 块设备：
  - [x] 把 namespace 暴露为块设备节点（/dev/nvme0n1），容量/扇区大小正确。
  - [x] 连接到 N1 的 request_queue（NVMe 的 request_fn/submit 路径），让 FS 不感知设备类型差异。
- [x] /dev 与 sysfs 对齐：
  - [x] devtmpfs 自动生成 /dev/nvme0n1。
  - [x] /sys/bus/pci 与 /sys/devices 下能看到 nvme 设备与 driver bind/unbind；unbind 后块设备不可用且资源释放可验证。
- [ ] 可观测与回归：
  - [ ] /proc/diskstats（或增强版 blockstats）提供 per-disk 统计（至少 nvme0n1 与 ram0 对比可见）。【延后】
  - [x] smoke 新增 NVMe minix 用例：挂载 /mnt（backend=/dev/nvme0n1），完成 N3 的 Minix RW 用例。

交付项：
- PCI/PCIe 枚举与能力解析收敛：设备发现稳定、BAR 资源与 MSI/MSI-X（或最小中断路径）可用，失败路径可观测。
- NVMe：识别 controller、枚举 namespace，并将 namespace 暴露为块设备（/dev/nvme0n1），容量/扇区大小正确。
- 块设备注册与 sysfs 可观测：/sys/devices 与 /sys/bus/pci 能反映 nvme 设备与 driver bind/unbind；/proc/diskstats（或增强版 blockstats）反映 per-disk I/O。
- 挂载链路：支持 `vfs_mount_fs_dev("/mnt", "minix", "/dev/nvme0n1")`，并跑通 N3 的读写回归。

验收：
- QEMU（带 NVMe 设备）下可稳定启动并挂载 /mnt（backend=/dev/nvme0n1），完成 Minix 读写回归。
- bind/unbind 可复现：unbind 后 /dev 节点消失或不可用；bind 后恢复；无崩溃、无资源泄漏。

## 2.1 落点映射表（建议文件/函数）

说明：这是“按 Linux 2.6 形态”组织的建议落点。实际以现有目录为准，优先做到“接口名/调用顺序可对照”，再逐步补齐语义。

### N0：address_space ops 与 VFS 边界

建议新增/修改文件：
- `include/linux/pagemap.h`：补充 `struct address_space_operations`、`struct address_space` 挂载点。
- `mm/filemap.c`：把 `generic_file_read/write` 调整为基于 `a_ops` 的通用路径；把 blockdev 特判下沉到 blockdev 自己的 `a_ops`/`f_ops`。
- `fs/block_dev.c`：blockdev inode 的 `f_ops`/`a_ops` 定义与注册（对接 N1/N2）。

关键函数/结构（建议命名）：
- `struct address_space_operations { readpage, writepage, write_begin, write_end, bmap }`（按阶段只实现子集）
- `generic_file_read_iter()` / `generic_file_write_iter()`（或保持现有命名但语义对齐）
- `blockdev_aops`、`blockdev_fops`（块设备专用）

### N1：bio/request_queue 最小块层

建议新增/修改文件：
- `include/linux/bio.h`：`struct bio`、`bio_alloc/bio_put`、`bio_endio`。
- `include/linux/blkdev.h`：扩展为 Linux-like 的块层入口（queue、gendisk/major/minor 可后置）。
- `block/blk-core.c`（或 `drivers/block/blkcore.c`）：`submit_bio()`、`generic_make_request()`、错误回传与统计。
- `block/ll_rw_blk.c`（或合并到上面）：`request_queue`、`make_request_fn`/`request_fn` 框架（先 noop）。
- `drivers/block/ramdisk.c`：实现 ramdisk 的 `request_fn`（从 request 中取 bio 并完成）。
- `fs/procfs/procfs.c`：增加 `/proc/diskstats`（优先）或把 `/proc/blockstats` 扩展为 per-disk。

关键函数/结构（建议命名）：
- `submit_bio(int rw, struct bio *bio)`
- `generic_make_request(struct bio *bio)`
- `blk_init_queue(request_fn_proc_t *rfn, void *queuedata)`
- `blk_execute_rq()`（同步执行可先做）
- `end_request(struct request *rq, int error)` / `bio_endio(struct bio *bio, int error)`

### N2：buffer cache（buffer_head）与块读写原语

建议新增/修改文件：
- `include/linux/buffer_head.h`：`struct buffer_head` 定义（或放入 `fs/buffer.c` 的本地头，后续再提升）。
- `fs/buffer.c`：`bread/brelse/mark_buffer_dirty/sync_dirty_buffer/ll_rw_block`（先同步）。
- `fs/block_dev.c`：把“块设备读写原语”收敛到 `ll_rw_block`/`submit_bio`。
- `mm/vmscan.c`：回收路径中引入 buffer cache 的回收钩子（先只回收 clean buffer）。

关键函数/结构（建议命名）：
- `struct buffer_head { b_blocknr, b_size, b_data, b_state, b_count, b_bdev }`
- `struct buffer_head *bread(struct block_device *bdev, uint32_t block)`
- `void brelse(struct buffer_head *bh)`
- `void mark_buffer_dirty(struct buffer_head *bh)`
- `int sync_dirty_buffer(struct buffer_head *bh)`
- `void ll_rw_block(int rw, int nr, struct buffer_head **bhs)`

### N3：Minix 真实格式读写（基于 buffer cache）

建议新增/修改文件：
- `fs/minixfs/minixfs.c`：从“演示镜像生成 + 只读读取”演进为真实格式读写；读写元数据与数据块必须走 `bread/ll_rw_block`。
- `include/linux/minix_fs.h`（可选）：抽出 on-disk 结构与 magic 常量，便于对照 Linux 2.6 `fs/minix/`。
- `usr/smoke.c`：新增 Minix RW 测试（create/overwrite/append/truncate/reopen 校验）。
- `init/initramfs.c`：支持携带 minix 镜像（例如 `/images/minix.img`），并把它作为块设备后端来源（避免运行时“生成”掩盖问题）。

关键函数/结构（建议命名）：
- `minix_read_super()` / `minix_write_super()`（写入可先最小化）
- `minix_iget()`（inode cache 可后置，但接口名可先收敛）
- `minix_lookup()` / `minix_readdir()` / `minix_create()` / `minix_unlink()`
- `minix_readpage()` / `minix_writepage()` 或 `minix_write_begin/write_end`（取决于 N0 选择的 a_ops 子集）
- `minix_get_block()`（将 file block → disk block 的映射逻辑集中化）

### N4：PCI/PCIe/NVMe → 块设备 → Minix 挂载

建议新增/修改文件：
- `drivers/pci/pci.c`：枚举稳定化（桥递归、BAR 资源、capability 框架）。
- `drivers/pci/pcie/pcie.c`：PCIe capability 解析与可观测（先用于 sanity check）。
- `drivers/nvme/nvme.c`：拆分为 controller/queue/namespace 子模块（可仍在一个文件内，先保证结构可读）。
- `drivers/base/{bus,driver,core}.c` + `fs/sysfs/sysfs.c`：bind/unbind 的资源释放路径补齐（NVMe 设备 remove 时需要）。
- `fs/devtmpfs/devtmpfs.c`：支持生成 `/dev/nvme0n1`（当前已支持 type=block 的 generic 逻辑，需确保命名/容量信息正确）。
- `/proc` 可观测：
  - `fs/procfs/procfs.c`：`/proc/diskstats`（优先）或增强版 per-disk `/proc/blockstats`。
  - `fs/procfs/procfs.c`：`/proc/pcie`（可选）导出 link/capability 信息用于调试。

关键函数/结构（建议命名）：
- PCI/PCIe：
  - `pci_scan_bus()` / `pci_scan_slot()` / `pci_read_config_*()`（现有实现可逐步收敛命名）
  - `pci_enable_device()`、`pci_set_master()`、`pci_request_regions()`（先实现最小语义）
  - `pci_enable_msix()` / `pci_enable_msi()`（至少一种稳定可用）
- NVMe（按 Linux 2.6 思路拆分）：
  - `nvme_probe()` / `nvme_remove()`
  - `nvme_init_ctrl()`（CAP/CC/CSTS.RDY）
  - `nvme_alloc_admin_queue()`、`nvme_identify_ctrl()`、`nvme_identify_ns()`
  - `nvme_alloc_io_queues()`、`nvme_submit_io()`、`nvme_poll_cq()`/`nvme_irq_handler()`
  - `nvme_register_ns_blockdev()`（namespace → /dev/nvme0n1）
- 挂载链路（目标）：
  - `vfs_mount_fs_dev("/mnt", "minix", "/dev/nvme0n1")`

### N5：后置阶段（写回线程模型 + swap/anon reclaim）（P5，推后）

目标：在 N0–N4 主链路稳定后，再把 writeback 与 swap 补齐到更 Linux 2.6 的后台模型与回压语义。

交付项：
- 后台 writeback：dirty 限额、周期写回、balance_dirty_pages 最小版回压、按脏 inode 遍历（避免全局扫描）。
- swap：swap cache/pageout/pagein；vmscan 在 file reclaim 不足时回收 anon 并能解释 OOM 决策。

验收：
- 压力用例：匿名页压力下系统仍可运行；swap in/out 统计稳定增长；无 silent corruption。

## 3. 强制回归基线（v2）

每次合并前必须满足：
- `make -j$(nproc)` 通过
- `make smoke-512` 通过（完整压力路径）
- `make smoke-128` 通过（低内存退化路径）
- 新增专项测试必须能在 30s 内稳定复现（避免偶现）

## 4. 非目标（仍然刻意延后）

- 网络栈、security、复杂 IPC。
- 完整 SMP/NUMA、高端内存（highmem/PAE）与复杂 I/O 调度器策略（先做框架）。
- journaling FS（ext3/ext4）与复杂块设备（DM/LVM/multipath）。
