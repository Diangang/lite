# Lite OS 内存布局梳理（现状）

本文档整理当前实现的物理内存布局、内核虚拟地址布局、用户态虚拟地址布局，以及三者之间的映射关系与关键不变量。

---

## 1. 物理内存布局（Physical）

### 1.1 固定/保留区域

- `0x00000000 ~ 0x000FFFFF`：低端保留（BIOS/实模式遗留区/VGA 等），PMM 初始化时默认不释放此范围（见 [pmm.c](file:///data25/lidg/lite/pmm.c#L98-L123)）。
- `0x00100000` 起：内核镜像加载基址（链接脚本把内核放到 1MB，见 [linker.ld](file:///data25/lidg/lite/linker.ld#L10-L54)）。
- InitRD 模块：由 Multiboot modules 传入，物理地址范围为 `mod->mod_start ~ mod->mod_end`（内核启动阶段读取并保证恒等映射，见 [kernel.c](file:///data25/lidg/lite/kernel.c#L581-L621)）。

### 1.2 PMM 元数据放置策略

PMM 的 bitmap 与 refcount 数组放在：

- `kernel_end = &end`（链接脚本导出的内核末尾符号）
- 同时考虑 multiboot module 描述数组与模块内容，取最大末尾
- 页对齐后放置 `pmm_bitmap`，随后紧跟 `pmm_refcount`

实现见 [pmm.c](file:///data25/lidg/lite/pmm.c#L40-L130)。

### 1.3 可用页释放原则（重要）

PMM 初始化时先把所有页标为“已占用”，然后遍历 BIOS E820 map 仅释放：

- 类型为 `MULTIBOOT_MEMORY_AVAILABLE`
- 且页地址 `page_addr >= meta_end_addr`（避免覆盖内核、模块和 PMM 元数据）

实现见 [pmm.c](file:///data25/lidg/lite/pmm.c#L89-L127)。

---

## 2. 内核虚拟地址布局（Kernel Virtual）

当前内核不是“纯高半区内核”布局，而是混合布局：

### 2.1 恒等映射（低端 0~128MB）

VMM 初始化时为 `0x00000000 ~ 0x08000000`（128MB）建立恒等映射：

- VA == PA
- 页表项为 `PTE_PRESENT | PTE_READ_WRITE`（Supervisor-only，不带 `PTE_USER`）

实现见 [vmm.c](file:///data25/lidg/lite/vmm.c#L28-L93)。

这意味着：

- 内核代码/数据（物理 1MB 起）用恒等映射直接访问
- InitRD / multiboot 结构等若落在 128MB 内也可直接访问
- 用户态禁止直接访问这些恒等映射页（因为没有 `PTE_USER`）

### 2.2 内核堆（3GB 起的虚拟映射）

内核堆虚拟基址固定在：

- `KHEAP_START = 0xC0000000`（见 [kheap.h](file:///data25/lidg/lite/kheap.h#L7-L11)）

堆扩展时从 PMM 分配物理页，并把这些物理页映射到 `0xC0000000` 起的虚拟地址区间：

实现见 [kheap.c](file:///data25/lidg/lite/kheap.c#L14-L80)。

这建立了一个关键边界：

- `0xC0000000` 以上默认视为“内核虚拟空间”（用户态指针校验也以此为界，见 [vmm.c](file:///data25/lidg/lite/vmm.c#L321-L349)）。

### 2.3 启动栈与调度栈

- boot 阶段：`boot.s` 使用 `.bootstrap_stack` 段预留 32KB 栈（见 [boot.s](file:///data25/lidg/lite/boot.s#L22-L28) 与 [linker.ld](file:///data25/lidg/lite/linker.ld#L44-L53)）。
- tasking 初始化后：每个 task 有自己独立的 4KB kernel stack（`kmalloc(4096)`）；任务切换时更新 `tss.esp0 = task->stack + 4096`（见 [task.c](file:///data25/lidg/lite/task.c#L860-L899)）。

---

## 3. 用户态虚拟地址布局（User Virtual）

### 3.1 用户地址空间上界

当前实现将 `0xC0000000` 作为用户态上界：

- ELF 加载检查：`p_vaddr + p_memsz < 0xC0000000`（见 [kernel.c](file:///data25/lidg/lite/kernel.c#L292-L307)）
- 用户指针校验：`addr + len - 1 < 0xC0000000`（见 [vmm.c](file:///data25/lidg/lite/vmm.c#L321-L349)）

### 3.2 用户程序加载基址（ELF 链接脚本）

用户态程序链接地址固定为：

- `0x40000000`（1GB 处，见 [userprog.ld](file:///data25/lidg/lite/usr/userprog.ld)）
（注：原设计为 `0x400000`，但为避免与内核 `0~128MB` 的恒等映射共享页表导致 execve 销毁空间时引发缺页冲突，现已提升至 `1GB` 处。）

内核加载器据 PT_LOAD 段计算 `user_base=min_vaddr` 与 `user_end=max_vaddr`，并按段权限建立 VMA（见 [kernel.c](file:///data25/lidg/lite/kernel.c)）。

### 3.3 用户栈

- 栈页基址：`user_stack_base = 0xBFFFF000`
- 初始 user esp：`0xC0000000`

实现见 [kernel.c](file:///data25/lidg/lite/kernel.c#L333-L411)。

### 3.4 用户堆（brk）

用户堆基址：

- `heap_base = user_end`（加载器计算）
- `heap_brk` 初值同 `heap_base`
- 并在 VMA 列表中加入一个空的 heap VMA（`start=end=heap_base`），随后 `brk()` 扩展 VMA.end

实现见 [kernel.c](file:///data25/lidg/lite/kernel.c#L333-L371) 与 [task.c](file:///data25/lidg/lite/task.c#L1090-L1136)。

不变量：

- `heap_base >= 0x1000`
- `heap_base < 0xC0000000`
- `align_up(heap_brk)+0x1000 <= user_stack_base`（防止与栈碰撞）

### 3.5 用户匿名映射（mmap/munmap）

`mmap(addr=0)` 的选址策略：

- 从 `align_up(heap_brk)` 开始扫描（若 heap 未初始化则从 `0x400000` 开始）
- 确保不小于 `user_base + user_pages*4096`（避开程序映像）
- 上界为 `user_stack_base`（或 `0xC0000000`）

实现见 [task.c](file:///data25/lidg/lite/task.c#L560-L576) 与 [task.c](file:///data25/lidg/lite/task.c#L352-L377)。

注意：mmap 只添加 VMA，真正的物理页分配在缺页时按需完成。

---

## 4. 关键映射与缺页语义（为什么能工作）

### 4.1 “恒等映射页”与“用户 VMA”冲突的处理（历史问题）

早期设计中，用户程序常用的 `0x400000` 区间落在内核 0~128MB 的恒等映射范围内。这会导致 fork/execve 时产生严重的页表共享销毁冲突（见 Issues.md 6.9）。

当前实现已将用户态程序的链接基址提升至 `0x40000000`（1GB 处），从根本上避开了与低端 128MB 内核恒等映射的重叠。因此，用户进程各自拥有完全独立的页目录和页表，不再发生互相干扰的越权或被销毁等情况。

### 4.2 COW

fork 后共享页写入触发 COW：

- PTE 带 `PTE_COW` 时写入会在 `vmm_copyout`/page fault 路径进行 resolve，分配新物理页并复制（见 [vmm.c](file:///data25/lidg/lite/vmm.c#L351-L378)）。

