# Lite OS 内存布局梳理

本文档整理当前实现的物理内存布局、内核虚拟地址布局、用户态虚拟地址布局，以及三者之间的映射关系。

---

## 1. 物理内存布局 (Physical)

### 1.1 固定/保留区域
- `0x00000000 ~ 0x000FFFFF`：低端保留（BIOS/实模式遗留区/VGA 等），页分配器初始化时默认不释放此范围。
- `0x00100000` 起：内核镜像加载基址，`boot` 段占用前部，内核主体从 `0x00100000 + sizeof(.text.boot)` 起。
- **InitRD 模块**：由 Multiboot modules 传入，内核启动阶段解析。

### 1.2 Page Alloc 元数据放置策略
位图与 refcount 数组放在：
- `kernel_end = &end`（链接脚本导出的内核末尾符号）与 multiboot module 描述数组取最大末尾之后。
- 页对齐后放置 buddy 元数据（`buddy_next`），由 `bootmem_alloc` 分配并保留。

### 1.3 可用页释放原则
页分配器初始化时先把所有页标为“已占用”，然后遍历 BIOS E820 map 仅释放类型为 `MULTIBOOT_MEMORY_AVAILABLE` 且不与内核/元数据冲突的物理页。

---

## 2. 内核虚拟地址布局 (Kernel Virtual)

### 2.1 4GB 轴上的内核虚拟地址区段（Lite）

当前项目采用经典 32 位 `3G/1G split`：
- 用户态：`0x00000000 ~ TASK_SIZE`（`TASK_SIZE = PAGE_OFFSET = 0xC0000000`）
- 内核态：`PAGE_OFFSET ~ 0xFFFFFFFF`

按 4GB 虚拟地址轴从低到高划分，可以理解为：

```
0xFFFFFFFF  +----------------------------------------------------+
            | Fixmap reserved                                    |
            | [FIXADDR_START .. 0xFFFFFFFF]                       |
0xFF000000  +-------------------- FIXADDR_START ------------------+
            | vmalloc                                             |
            | [vmalloc_start .. vmalloc_end)                      |
            | vmalloc_end = FIXADDR_START                         |
            | vmalloc_start = align_up(directmap_end + 8MB, 4KB)  |
            |   where 8MB = VMALLOC_OFFSET                        |
            |                                                    |
directmap_end+-------------------- directmap_end ------------------+
            | Direct map (linear mapping of lowmem RAM)           |
            | [PAGE_OFFSET .. directmap_end)                       |
0xC0000000  +-------------------- PAGE_OFFSET --------------------+
            | User space (per-process mappings)                   |
0x00000000  +----------------------------------------------------+
```

其中：
- `PAGE_OFFSET = 0xC0000000`，`VMALLOC_OFFSET = 8MB`，`FIXADDR_START = 0xFF000000`
- `directmap_end = PAGE_OFFSET + align_up(bootmem_lowmem_end(), 4MB)`

这些边界的计算在 `memlayout_*()` 中完成，并在 `paging_init()` 打印校验（例如会检查 direct map 与 vmalloc 不重叠）。

### 2.2 内核线性映射（Direct map / lowmem）

启动开启分页时会短暂建立低端恒等映射用于过渡（当前实现映射前 4MB），并在 trampoline 中立即清理低端映射后进入内核高半区执行。

进入 C 阶段后，`paging_init()` 会建立内核的 direct map：
- 映射关系：`VA = PA + PAGE_OFFSET`
- 覆盖范围：`PA ∈ [0, bootmem_lowmem_end)`（按 4MB 边界向上取整）
- 页表项：`PTE_PRESENT | PTE_READ_WRITE`（Supervisor-only）

这条 direct map 用于覆盖 lowmem 范围内的 RAM。超过 `bootmem_lowmem_end` 的 RAM 目前不会纳入 direct map（也没有实现 Linux 风格的 highmem 临时映射机制）。

### 2.3 内核堆 (PAGE_OFFSET 起的虚拟映射)
内核堆虚拟基址固定在 `KHEAP_START = PAGE_OFFSET`（当前为 `0xC0000000`）。
- `kmalloc` 动态向后分配，每次从页分配器取页并映射到该虚拟地址区间。
- 这个地址作为内核态与用户态的一个重要软边界（用户态指针不得超过此边界）。

### 2.4 Page Cache (文件系统页缓存)
- 新增的文件系统 Page Cache 直接使用 `alloc_page(GFP_KERNEL)` 获取物理页。
- 读写文件数据时，依赖高半区线性映射通过物理地址（`p->phys_addr`）进行 `memcpy`。
- **注意**：如果未来支持大于 128MB 的高端内存，Page Cache 读写需要引入临时映射（类似 Linux 的 `kmap_atomic`）。

### 2.5 Linux 2.6 i386（参考）4GB 轴上的典型区段

Linux 2.6（i386，经典 3G/1G split）同样有 `PAGE_OFFSET = 0xC0000000`，但当开启 `CONFIG_HIGHMEM` 时，会在 vmalloc 与 fixmap 之间再插入一段 “persistent kmap（pkmap）” 区域用于 highmem 的 `kmap()`。

按 4GB 虚拟地址轴从低到高的典型顺序可概括为：

```
0xFFFFFFFF  +------------------------------------------------------------+
            | FIXMAP area (fixed_addresses + boot/temp fixmaps)          |
FIXADDR_TOP +-------------------------- FIXADDR_TOP ----------------------+
            | permanent fixed mappings                                   |
FIXADDR_START+------------------------- FIXADDR_START --------------------+
            | boot fixmap / temp fixmap                                  |
FIXADDR_BOOT_START
            +---------------------- FIXADDR_BOOT_START -------------------+
            | Persistent kmap area (pkmap)                                |
            |   [PKMAP_BASE .. FIXADDR_BOOT_START)                        |
PKMAP_BASE   +--------------------------- PKMAP_BASE ---------------------+
            | VMALLOC area                                                 |
            |   [VMALLOC_START .. VMALLOC_END)                             |
VMALLOC_START+------------------------ VMALLOC_START ---------------------+
            | lowmem direct map area                                      |
            |   [PAGE_OFFSET .. high_memory)                               |
PAGE_OFFSET  +-------------------------- PAGE_OFFSET ---------------------+
0x00000000  +------------------------------------------------------------+
```

其中：
- `PAGE_OFFSET` 在 i386 上通常也是 `0xC0000000`
- `VMALLOC_START` 由 `high_memory + vmalloc_earlyreserve + 2*VMALLOC_OFFSET` 对齐得到
- `VMALLOC_END` 在 `CONFIG_HIGHMEM=y` 时由 `PKMAP_BASE` 限定，否则由 `FIXADDR_START` 限定
- `PKMAP_BASE`、`FIXADDR_START/TOP` 由 fixmap/highmem 相关宏计算得到

---

## 3. 用户态虚拟地址布局 (User Virtual)

### 3.1 用户态上限
- 用户态最高可用虚拟地址为 `TASK_SIZE`（当前为 `0xC0000000`）。所有系统调用传递的用户指针均需在此边界下。

### 3.2 用户程序加载基址 (ELF)
- 用户态程序链接地址固定为 `0x40000000`（1GB 处）。
- 这种高位设计彻底避开了与内核高半区线性映射的页表冲突，使 `fork`/`execve` 时的页目录销毁变得安全独立。

### 3.3 用户栈与堆
- **栈 (Stack)**：栈底位于 `USER_STACK_BASE`（当前为 `0xBFFFF000`），向下生长。初始 `esp` 设为 `USER_STACK_TOP`（当前为 `0xC0000000`）。
- **堆 (Heap/BRK)**：紧接在 ELF 的 `.bss` 段之后（`start_brk`）。通过 `brk()` 系统调用向上扩展。

### 3.4 匿名映射 (mmap)
- `mmap` 的选址策略为从 `align_up(brk)` 开始向上寻找空闲的 VMA，最高不超过用户栈底。

### 3.5 NVMe 设备内存映射
- NVMe 控制器和命名空间的内存映射通过 PCIe BAR (Base Address Register) 实现
- 控制器寄存器和门铃区域通过 `ioremap` 映射到内核虚拟地址空间
- 命名空间数据通过块设备接口访问，最终通过 PCIe DMA 传输

### 3.6 文件系统内存映射
- **Page Cache**：文件数据通过 page cache 缓存，使用物理页分配器分配物理页
- **MinixFS**：元数据和数据块通过 buffer cache 管理，与 page cache 集成
- **ramfs**：完全基于内存的文件系统，直接使用内核内存分配
- **procfs/sysfs/devtmpfs**：伪文件系统，数据动态生成，不占用实际磁盘空间

---

## 4. 关键机制说明

### 4.1 Copy-On-Write (COW)
- `fork` 后父子进程共享物理页，PTE 被标记为 `PTE_COW` 并设为只读。
- 任意一方写入触发 Page Fault 时，在异常处理路径中分配新页、拷贝数据并恢复读写权限。

### 4.2 文件系统数据在内存中的流转
- 当用户调用 `read` 或 `write` 系统调用时：
  1. 系统调用进入内核态，指针检查无误。
  2. VFS 的 `generic_file_read/write` 会根据偏移量计算出 `page_index`。
  3. 如果命中 Page Cache，直接从对应的物理页帧将数据 `memcpy` 到用户态的虚拟缓冲区。
  4. 如果未命中，分配新的物理页加入 `address_space` 并清零（或从磁盘读取），然后再拷贝。
