# Lite OS 项目开发与调试问题日志汇总

本文档按时间顺序汇总了 Lite OS 项目从零开始开发至今，所遇到的所有核心问题、排查思路及最终的解决方案。此文档旨在作为后续开发和学习的重要参考。

---

## 1. 基础环境与引导阶段问题

### 1.1 `make run` 无法启动（Grub/QEMU 兼容性）
- **现象**：早期尝试直接通过 GRUB 或原始 QEMU 命令启动时，系统无法正常引导。
- **定位**：Multiboot 头（`boot.s`）的 Magic Number 必须在文件的前 8KB 内，且必须 4 字节对齐。链接脚本 (`linker.ld`) 中段的顺序或起始地址不正确。
- **解决**：完善了 `linker.ld`，指定起始地址为 `0x100000` (1MB)，并将 `.multiboot` 段放置在最前面。使用 `qemu-system-i386 -kernel myos.bin` 直接以 Multiboot 协议引导，跳过 GRUB 打包步骤。

### 1.2 C 库缺失导致的编译失败
- **现象**：在 `kernel.c` 中使用 `printf` 等标准函数时，GCC 报错 `undefined reference`。
- **定位**：由于使用了 `-ffreestanding` 编译选项，系统不链接标准 C 库 (`libc`)。
- **解决**：从零实现了极简的 `libc`，包括 `memcpy`, `memset`, `strlen`, `strcmp`, `itoa` 以及自定义的可变参数 `printf`。

---

## 2. 内存管理 (PMM & VMM) 阶段问题

### 2.1 物理内存管理器 (PMM) 破坏内核代码
- **现象**：系统在分配物理页后出现随机崩溃。
- **定位**：PMM 的 Bitmap（位图）存放位置覆盖了内核的 `.bss` 段或代码段。
- **解决**：在 `linker.ld` 中暴露 `end` 符号，并在 `pmm.c` 中将 Bitmap 强制放置在 `&end` 之后。在初始化位图时，将 `0x0` 到 `bitmap_end` 之间的所有物理页均标记为“已占用”。

### 2.2 开启分页 (Paging) 后瞬间 Triple Fault
- **现象**：在 `vmm_init` 中设置 `CR0` 寄存器的 `PG` 位（开启分页）后，QEMU 立即无限重启。
- **定位**：开启分页的瞬间，CPU 期望 EIP（指令指针）所在的地址必须在页表中有效。如果没有建立恒等映射（Identity Mapping），CPU 取下一条指令时会触发缺页异常（Page Fault），由于 IDT 尚未完全接管，直接导致 Triple Fault。
- **解决**：在开启分页前，强制将物理地址 `0x000000` - `0x400000`（前 4MB）映射到相同的虚拟地址。

---

## 3. 中断与外设驱动阶段问题

### 3.1 键盘按键无反应 / 异常冲突
- **现象**：按下键盘没有触发中断，或者触发了 CPU 异常（如 Double Fault）。
- **定位**：x86 架构中，BIOS 默认将 PIC (8259A) 的 IRQ 0-7 映射到中断号 8-15。但这与保护模式下 CPU 保留的异常（如 Int 8 是 Double Fault，Int 14 是 Page Fault）冲突。
- **解决**：在 `isr.c` 中实现 `pic_remap`，向主从 PIC 发送 ICW 命令，将 IRQ0-7 重新映射到 32-39，IRQ8-15 映射到 40-47。

### 3.2 键盘只能按一次
- **现象**：按下一个键后，屏幕打印了字符，但后续再按任何键都没反应。
- **定位**：处理完硬件中断后，没有向 PIC 发送 EOI（End Of Interrupt）信号，导致 PIC 认为上一个中断还在处理，屏蔽了后续中断。
- **解决**：在 `irq_handler` 的末尾添加 `outb(0x20, 0x20)`（主片 EOI）和 `outb(0xA0, 0x20)`（从片 EOI）。

---

## 4. 文件系统与 InitRD 阶段问题

### 4.1 InitRD 加载导致的 Triple Fault
- **现象**：在使用 `qemu -initrd initrd.img` 启动后，系统在进入 `init_initrd` 函数前无限重启。
- **定位**：通过引入串口日志（`serial_write`），发现内核试图读取地址 `0xFFFFFFFF` 的数据。
- **根因**：错误解引用了 Multiboot 结构体。`mbi->mods_addr` 是一个指向 `multiboot_module_t` 结构体数组的**指针**，而非文件数据的物理地址。
- **解决**：
  1. 修正指针算术：`uint32_t initrd_location = ((multiboot_module_t*)mbi->mods_addr)->mod_start;`
  2. 在 `vmm.c` 中扩展恒等映射到 128MB，确保 InitRD 被加载的高端内存地址可以直接被内核访问。
  3. 在 `pmm.c` 中保护 InitRD 所在的物理内存不被当作空闲内存分配。

### 4.2 解析 InitRD 文件名乱码
- **现象**：`ls` 命令输出的文件名包含乱码。
- **定位**：`initrd_readdir` 复制字符串时没有正确添加 `\0` 终止符。
- **解决**：在 `initrd_readdir` 中强制添加 `dirent.name[strlen(...)] = 0;`。

---

## 5. 无头模式 (Headless / Serial) 交互问题

### 5.1 Shell 对键盘输入无响应（串口模式）
- **现象**：使用 `qemu -serial stdio -display none` 运行，系统能输出日志到终端，但输入 `ls` 没有反应。
- **定位**：在 `-display none` 模式下，QEMU 将终端输入重定向到虚拟串口 (COM1)，而不是 PS/2 键盘。但内核只有 IRQ 1（键盘）中断，没有处理 IRQ 4（串口）。
- **解决**：
  1. **驱动实现**：在 `interrupt.s` 和 `isr.c` 中添加对 `irq4` 的支持，映射到 IDT 36。
  2. **中断开启**：在 `serial_init` 中设置 UART 寄存器开启“接收数据可用”中断，并在 PIC 掩码中解蔽 IRQ 4。
  3. **数据读取**：实现 `serial_handler`，读取 `0x3F8` 端口并传入 Shell。

### 5.2 回车键和退格键无效
- **现象**：启用串口后，输入字符可见，但按回车不执行，按退格无法删除。
- **定位**：不同终端发送的回车符不同（如 `\r` 或 `\n`），且退格键可能是 `\b` 或 `0x7F` (DEL)。原 `shell.c` 仅支持 `\n` 和 `\b`。
- **解决**：修改 `shell.c` 的字符处理逻辑：
  - 将 `if (c == '\n')` 改为 `if (c == '\n' || c == '\r')`。
  - 将 `if (c == '\b')` 改为 `if (c == '\b' || c == 0x7F)`。

### 5.3 变量未使用告警
- **现象**：编译时提示 `warning: unused variable ‘val’`。
- **定位**：为了测试内存是否可读，定义了 `uint8_t val = byte_ptr[i];` 但未参与后续计算。
- **解决**：将代码修改为 `(void)byte_ptr[i];`，既保留了内存访问测试，又消除了编译告警。

---

## 6. 用户态与独立页表问题

### 6.1 用户态启动后 Page Fault (read user)
- **现象**：执行 `user` 后出现 `Page Fault! ( read user ) at 0x104000`，随后 `KERNEL PANIC: Unhandled Page Fault`。
- **定位**：用户态访问的页表项虽然设置了 `PTE_USER`，但对应的页目录项仍是内核权限，CPU 认为该页目录不可在 Ring 3 访问。
- **解决**：在映射用户页时同步设置 PDE 的 `PTE_USER` 位，确保 PDE 与 PTE 同时具备用户权限。

### 6.2 用户态 ELF 加载出现大量 Page Fault
- **现象**：执行 `user` 后出现大量 `Page Fault! ( not-present write kernel )`，最后 `Page Fault! ( read user )` 并 `KERNEL PANIC`。
- **定位**：用户 ELF 含 `.note.gnu.property` 段，链接器将其映射到 `0x08048000` 附近，加载器按段映射导致用户访问落在未预期区域，引发连锁缺页。
- **解决**：为用户程序提供独立链接脚本，丢弃 `.note*` 段，只保留 `.text/.rodata/.data/.bss`，确保 ELF PT_LOAD 段落在 `0x400000` 附近并稳定可控。

### 6.3 用户态 ELF 段越界检查导致加载失败
- **现象**：执行 `user` 提示 `User program segment out of range.`。
- **定位**：ELF 中存在 `p_filesz=0` 的 BSS 段，但 `p_offset` 仍位于文件尾部之后，加载器用 `p_offset + p_filesz > file_size` 直接判定越界，导致误报。
- **解决**：仅在 `p_filesz > 0` 时进行 `p_offset + p_filesz` 的范围检查，允许纯 BSS 段通过。

### 6.4 内核态 syscall 演示无输出
- **现象**：在 shell 执行 `syscall` 没有任何输出。
- **定位**：`syscall` 命令在内核态触发 `int 0x80`，传入的字符串指针位于内核地址空间；而 syscall 层新增了用户指针校验，把该指针当作非法用户指针拒绝，导致 `SYS_WRITE` 没有打印。
- **解决**：仅当 syscall 来自 Ring3 时启用用户指针校验；内核态触发的 `int 0x80` 允许使用内核指针用于演示。

### 6.5 用户态 shell 进入即 Page Fault（栈页基址设置错误）
- **现象**：执行 `run ush.elf` 后立刻触发 `Page Fault! ( not-present write user ) at 0xbffffffc`，并显示 `User Page Fault: out of range.`，用户任务被终止。
- **定位**：该地址位于用户栈顶页（`0xBFFFF000` - `0xBFFFFFFF`）。但加载器在建立用户 VMA/映射时，把 `user_stack_base` 误写成 `0xBFF000`，导致：
  - VMA 仅覆盖低地址的“栈页”，不包含 `0xBFFFF000`；
  - `enter_user_mode()` 传入的用户栈顶固定为 `0xC0000000`，首次压栈必然写到 `0xBFFFFFFC`，触发缺页；
  - 缺页处理路径检查 VMA 失败，判定 out-of-range 并 kill。
- **解决**：将 `load_user_program` 中的 `user_stack_base` 修正为 `0xBFFFF000`，使栈 VMA 与实际栈顶一致（同时映射该页）。

### 6.6 用户态 mmap 写入触发 present fault（低端恒等映射权限冲突）
- **现象**：执行 `mmap.elf` 后输出 `Page Fault! ( write user ) at 0x403000`，并显示 `User Page Fault: unhandled.`。
- **定位**：用户 VMA 位于 `0x400000` 低端区域，但内核在 `vmm_init` 里对 `0~128MB` 做了 supervisor-only 恒等映射。用户写入时触发“present but no user permission”类型缺页，现有缺页处理只处理 not-present 分支，导致直接杀死用户进程。
- **解决**：在缺页处理里增加 “present fault 且 VMA 允许访问” 的修正路径：为该页重新分配物理页，设置 `PTE_USER` 并按 VMA 设定读写权限，确保用户映射可写。

### 6.7 fork/COW 写入触发两次 Page Fault（写时复制预期现象）
- **现象**：执行 `fork.elf` 时打印两次 `Page Fault! ( write user )`，并显示 `Page Fault handled: remapped ...`，随后父子均正常输出。
- **定位**：父子进程在 fork 后共享同一用户页；首次写入触发 COW，内核为当前进程分配新页并重新映射，因此各自都会触发一次缺页。
- **解决**：属预期行为，无需修复；可通过 `/proc/cow` 观察 faults/copies 计数随写入增加。

---

## 总结

在从零构建 Lite OS 的过程中，最困难的环节往往是“**缺乏可见性**”（如早期的 Triple Fault 导致无法看报错）。
最有效的应对策略是：
1. **尽早建立可靠的输出通道**：如优先实现 Serial 串口输出，它比 VGA 更底层、更可靠，能在异常崩溃时保留最后的遗言。
2. **步步为营**：在开启分页、中断等危险操作前，必须进行详尽的地址计算和状态检查。
3. **查阅官方规范**：如 Multiboot 协议的结构体定义、x86 PIC 的端口操作手册等。想当然的指针转换往往是致命 Bug 的根源。
