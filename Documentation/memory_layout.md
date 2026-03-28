# Lite OS 内存布局梳理

本文档整理当前实现的物理内存布局、内核虚拟地址布局、用户态虚拟地址布局，以及三者之间的映射关系。

---

## 1. 物理内存布局 (Physical)

### 1.1 固定/保留区域
- `0x00000000 ~ 0x000FFFFF`：低端保留（BIOS/实模式遗留区/VGA 等），页分配器初始化时默认不释放此范围。
- `0x00100000` 起：内核镜像加载基址。
- **InitRD 模块**：由 Multiboot modules 传入，内核启动阶段解析。

### 1.2 Page Alloc 元数据放置策略
位图与 refcount 数组放在：
- `kernel_end = &end`（链接脚本导出的内核末尾符号）与 multiboot module 描述数组取最大末尾之后。
- 页对齐后放置 buddy 元数据（`buddy_next`），由 `bootmem_alloc` 分配并保留。

### 1.3 可用页释放原则
页分配器初始化时先把所有页标为“已占用”，然后遍历 BIOS E820 map 仅释放类型为 `MULTIBOOT_MEMORY_AVAILABLE` 且不与内核/元数据冲突的物理页。

---

## 2. 内核虚拟地址布局 (Kernel Virtual)

### 2.1 恒等映射 (低端 0~128MB)
Paging 初始化时为 `0x00000000 ~ 0x08000000`（128MB）建立恒等映射：
- VA == PA
- 页表项为 `PTE_PRESENT | PTE_READ_WRITE` (Supervisor-only)。
- 内核代码/数据、InitRD、页表目录等若落在该区域，均通过直接恒等映射访问。

### 2.2 内核堆 (3GB 起的虚拟映射)
内核堆虚拟基址固定在 `KHEAP_START = PAGE_OFFSET`（当前为 `0xC0000000`）。
- `kmalloc` 动态向后分配，每次从页分配器取页并映射到该虚拟地址区间。
- 这个地址作为内核态与用户态的一个重要软边界（用户态指针不得超过此边界）。

### 2.3 Page Cache (文件系统页缓存)
- 新增的文件系统 Page Cache 直接使用 `alloc_page(GFP_KERNEL)` 获取物理页。
- 读写文件数据时，依赖底层的恒等映射直接通过物理地址（`p->phys_addr`）进行 `memcpy`。
- **注意**：如果未来支持大于 128MB 的高端内存，Page Cache 读写需要引入临时映射（类似 Linux 的 `kmap_atomic`）。

---

## 3. 用户态虚拟地址布局 (User Virtual)

### 3.1 用户态上限
- 用户态最高可用虚拟地址为 `TASK_SIZE`（当前为 `0xC0000000`）。所有系统调用传递的用户指针均需在此边界下。

### 3.2 用户程序加载基址 (ELF)
- 用户态程序链接地址固定为 `0x40000000`（1GB 处）。
- 这种高位设计彻底避开了与内核 `0~128MB` 恒等映射的页表冲突，使 `fork`/`execve` 时的页目录销毁变得安全独立。

### 3.3 用户栈与堆
- **栈 (Stack)**：栈底位于 `USER_STACK_BASE`（当前为 `0xBFFFF000`），向下生长。初始 `esp` 设为 `USER_STACK_TOP`（当前为 `0xC0000000`）。
- **堆 (Heap/BRK)**：紧接在 ELF 的 `.bss` 段之后（`start_brk`）。通过 `brk()` 系统调用向上扩展。

### 3.4 匿名映射 (mmap)
- `mmap` 的选址策略为从 `align_up(brk)` 开始向上寻找空闲的 VMA，最高不超过用户栈底。

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
