# Linux 2.6 Alignment Roadmap

## Purpose

This is the current forward plan for aligning Lite with Linux 2.6. It replaces
the previous scattered roadmaps and status documents now archived under
`Documentation/archived/`.

The plan is intentionally conservative: keep the kernel runnable, align one
boundary at a time, and avoid creating Lite-only abstractions that Linux does
not have.

## Rules

- Read the Linux 2.6 reference first, then the Lite implementation.
- Keep the change scope to one subsystem boundary or one explicit gap.
- Preserve runnable state after every stage.
- Prefer placement, naming, lifetime, and call-flow convergence over feature
  expansion.
- Do not introduce a new abstraction unless Linux 2.6 has the same concept.
- Do not broaden APIC/SMP, block scheduling, SCSI EH, or writeback before their
  prerequisites are in place.
- Validate each implementation stage with at least `make -j4`; run
  `make smoke-128` and `make smoke-512` for storage, VFS, driver, memory, or
  scheduler changes.

## Stage 0: Documentation Baseline

Status: `DONE`

Scope:

- Move historical/debug/superseded documents to `Documentation/archived/`.
- Keep current top-level docs small and authoritative.
- Add `Current-State.md` and this roadmap.

Exit criteria:

- `Documentation/README.md` clearly identifies current documents.
- Old plans and debug sessions are no longer mixed with active docs.

## Stage 1: NVMe Host and Storage Debug Cleanup

Status: `ACTIVE`

Scope:

- `drivers/nvme/host/pci.c`
- `drivers/nvme/host/nvme.h`
- narrowly related storage debug prints if they only exist for NVMe smoke
  stabilization.

Goals:

- Finish NVMe host placement cleanup.
- Remove or gate stale `TRAEDBG` instrumentation.
- Keep NVMe namespace registration, devtmpfs/sysfs exposure, and minix smoke
  path stable.
- Fix narrow correctness issues found in the current storage path if they block
  smoke reliability.

Do not do:

- Do not redesign block core.
- Do not add blk-mq or a new I/O scheduler.
- Do not expand NVMe to MSI-X or multi-queue yet.
- Do not broaden SCSI/virtio while finishing NVMe.

Exit criteria:

- `make -j4`
- `make smoke-128`
- `make smoke-512`
- Root documentation remains consistent with the observed behavior.

## Stage 2: Init Flow and Initcall Ordering

Status: `PENDING`

Reference:

- `linux2.6/init/main.c`
- `linux2.6/include/linux/init.h`

Goals:

- Re-audit `start_kernel()`, `rest_init()`, `kernel_init()`, and initcall level
  execution.
- Tighten command-line parsing placement and initialization order.
- Make initcall ownership clear enough for later driver-core and storage work.

Exit criteria:

- Initcall order is documented and matches source.
- No driver or block semantic changes are mixed into this stage.
- Build and smoke pass.

## Stage 3: Core Synchronization Foundations

Status: `PENDING`

Reference:

- `linux2.6/include/linux/spinlock.h`
- `linux2.6/include/linux/atomic.h`
- `linux2.6/include/linux/wait.h`
- `linux2.6/include/linux/completion.h`

Goals:

- Establish stronger primitives for later task, waitqueue, driver-core, block,
  and interrupt work.
- Make atomic/refcount/spinlock semantics explicit even if the runtime remains
  UP for now.
- Prepare for safer object lifetime and queue manipulation.

Exit criteria:

- Primitive semantics are documented.
- Existing callers are adjusted only where required.
- Build and smoke pass.

## Stage 4: MM Second Pass

Status: `PENDING`

Reference:

- `linux2.6/mm/page_alloc.c`
- `linux2.6/mm/slab.c`
- `linux2.6/mm/vmalloc.c`
- `linux2.6/mm/rmap.c`
- `linux2.6/lib/radix-tree.c`
- `linux2.6/lib/idr.c`

Goals:

- Improve allocator, vmalloc, rmap, and reclaim semantics.
- Replace simplified IDR/radix behavior where it blocks device or VFS work.
- Keep highmem/PAE/NUMA out of scope unless explicitly approved.

Exit criteria:

- Existing user processes, COW, VFS, and storage smoke paths still pass.

## Stage 5: Kernel Core Remaining Gaps

Status: `PENDING`

Reference:

- `linux2.6/kernel/sched.c`
- `linux2.6/kernel/fork.c`
- `linux2.6/kernel/exit.c`
- `linux2.6/kernel/signal.c`

Goals:

- Improve task lifetime and references.
- Tighten tasklist/waitqueue/signal semantics.
- Prepare for eventual per-CPU and SMP work without implementing SMP early.

Exit criteria:

- Process, shell, and smoke behavior remains stable.

## Stage 6: APIC, IOAPIC, and SMP Runtime

Status: `PENDING_AFTER_STAGE_5`

Reference:

- `linux2.6/arch/x86/kernel/apic/*`
- `linux2.6/arch/x86/kernel/smp.c`
- `linux2.6/arch/x86/include/asm/irq_vectors.h`

Dependency:

- Run after synchronization primitives, memory management, and kernel core task
  lifetime work are complete enough to support APIC/SMP semantics.

Goals:

- LAPIC timer.
- IPI send/receive path.
- IOAPIC routing.
- Per-CPU IRQ accounting.
- SMP scheduler interaction.

Exit criteria:

- APIC/IOAPIC behavior is documented against the implemented runtime.
- SMP work remains scoped to prerequisites already present in Lite.
- Build and smoke pass.

## Stage 7: Block Core Correctness Pass

Status: `PENDING`

Reference:

- `linux2.6/block/blk-core.c`
- `linux2.6/block/blk-sysfs.c`
- `linux2.6/include/linux/blkdev.h`
- `linux2.6/include/linux/blk_types.h`
- `linux2.6/block/genhd.c`

Goals:

- Fix request completion and queue accounting edge cases.
- Make bounds checks overflow-safe.
- Tighten `bdget`/`bdput`, `blkdev_get`/`blkdev_put`, inode lifetime, and
  whole-disk bdev semantics.
- Introduce only the minimal scheduler/elevator boundary needed for Linux-shaped
  layering, not a full scheduler implementation.

Known current candidates:

- Ensure every fetched request is completed or explicitly requeued.
- Audit `blockdev_inode_create()` failure unwinding.
- Audit 32-bit offset calculations in block, SCSI, and NVMe request paths.
- Keep block sysfs frozen except for Linux-existing attributes already modeled.

Exit criteria:

- Build and both smoke targets pass.
- Storage path remains compatible with ramdisk, SCSI disk, and NVMe namespace.

## Stage 8: Buffer Cache, Page Cache, and Writeback

Status: `PENDING`

Reference:

- `linux2.6/fs/buffer.c`
- `linux2.6/mm/filemap.c`
- `linux2.6/mm/page-writeback.c`
- `linux2.6/mm/backing-dev.c`

Goals:

- Clarify buffer_head ownership and dirty/writeback semantics.
- Align page cache and block device writeback paths.
- Introduce a minimal BDI-shaped boundary only when the call sites need it.
- Improve reclaim/writeback coupling without pretending to implement full
  Linux 2.6 writeback.

Exit criteria:

- Minix and block device smoke paths pass.
- Dirty page and buffer cache behavior is documented in `Current-State.md` or a
  focused reference doc if needed.

## Stage 9: Device Model and Sysfs Lifetime

Status: `PENDING`

Reference:

- `linux2.6/drivers/base/*`
- `linux2.6/fs/sysfs/*`

Goals:

- Tighten device/kobject/class/bus lifetime and release behavior.
- Reduce active debug traces in deferred probe and device attach paths.
- Clarify sysfs attribute group ownership and symlink semantics.
- Keep devtmpfs creation/removal tied to device lifecycle.

Exit criteria:

- Device registration/removal paths do not leak obvious object references.
- Storage devices still appear under `/dev` and `/sys`.
- Build and smoke pass.

## Stage 10: PCI, Virtio, SCSI Peripheral Alignment

Status: `PENDING`

Reference:

- `linux2.6/drivers/pci/*`
- `linux2.6/drivers/virtio/*`
- `linux2.6/drivers/scsi/*`

Goals:

- Improve PCI resource and capability ownership without implementing full PCIe.
- Keep virtio transport/frontend split Linux-shaped.
- Move more SCSI scan policy into SCSI midlayer rather than frontend drivers.
- Avoid full SCSI EH until timer, workqueue, and synchronization foundations are
  stronger.

Exit criteria:

- Virtio-scsi still produces `/dev/sda` in smoke.
- NVMe and SCSI can coexist in one smoke boot.

## Stage 11: Console, TTY, and Printk

Status: `PENDING`

Reference:

- `linux2.6/kernel/printk.c`
- `linux2.6/drivers/serial/*`
- `linux2.6/drivers/char/tty_*`

Goals:

- Align printk buffering and console registration boundaries.
- Clarify tty driver, line discipline, and serial ownership.
- Keep VT/pty/session semantics out of scope unless this stage is explicitly
  expanded.

Exit criteria:

- Serial console and shell interaction remain stable.

## Working Definition of Done

A stage is complete only when:

- source changes match the stage scope;
- related documentation is updated;
- `make -j4` passes;
- relevant smoke targets pass or the failure is documented with a current
  blocker;
- no new top-level historical/debug document is left in `Documentation/`.
