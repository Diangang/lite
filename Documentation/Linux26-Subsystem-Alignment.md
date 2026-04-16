# Linux 2.6 Subsystem Alignment (Plans + Gaps)

This document records subsystem-level alignment gaps and staged plans to converge Lite Kernel toward Linux 2.6 concepts, naming, semantics, and lifecycles.

## Progress Matrix (Snapshot)

Legend:
- `DONE`: implemented + kept passing `make -j4 && make smoke-512`
- `IN PROGRESS`: partial implementation exists; more stages remain
- `STAGE0 ONLY`: alignment report exists; implementation stages are not started
- `NOT STARTED`: no report and no implementation stages

Subsystem status:
- NVMe: `DONE` (Stages 1-4)
- Virtio: `DONE` (Stages 1-5)
- PCI/PCIe: `DONE` (Stages 1-4)
- Block Layer: `DONE` (Stages 0-3)
- VFS + Namespace + Dcache: `DONE` (Stages 0-4)

Filesystems:
- devtmpfs: `DONE` (Stages 0-2)
- ramfs: `STAGE0 ONLY` (needs Stage 1)
- minixfs: `STAGE0 ONLY` (needs Stages 1-2)
- procfs: `DONE` (Stages 0-2)
- sysfs: `IN PROGRESS` (Stage 1 DONE; Stage 2 partial)

Memory management (mm/):
- allocator/zones/bootmem: `DONE` (Stages 0-2)
- rmap/reclaim/swap: `IN PROGRESS` (Stages 0-2 DONE; Stage 3 pending)
- slab/kmalloc: `DONE` (Stages 0-1)
- vmalloc/ioremap: `DONE` (Stages 0-1)
- pagecache/writeback: `DONE` (Stages 0-2)
- page tables/fault/COW (mm/memory.c): `DONE` (Stages 0-2)
- VMA management (mm/mmap.c): `IN PROGRESS` (Stages 0-1 DONE; Stage 2 partial)

Kernel / arch / driver core:
- kernel core (printk): `IN PROGRESS` (Stage 0 DONE; small Linux-like semantic fixes landed)
- kernel core (syscalls): `IN PROGRESS` (Stage 0 DONE; Stage 1 partial)
- kernel core (sched/fork/exit/wait/signal): `STAGE0 ONLY` (needs Stage 1+)
- lib/: `IN PROGRESS` (Stage 1 partial; Stage 0 report pending)
- arch/x86: `STAGE0 ONLY` (needs Stage 1+)
- drivers/base: `IN PROGRESS` (Stage 0 DONE; Stage 1 partial; Stage 2 partial)
- timekeeping/clocksource: `IN PROGRESS` (Stage 0 DONE; Stage 1 partial)
- input/tty/console: `STAGE0 ONLY` (needs Stage 1+)

Next focus (priority order):
- 1) mm/memory.c + mm/mmap.c (fault/COW + VMA ordering), then reclaim/pagecache/vmalloc/slab/allocator

Principles:
- Do not invent new concepts/terms. Use Linux terminology and map each public behavior to a Linux counterpart.
- If a subsystem is simplified, explicitly mark it as DIFF and explain Why/Impact/Plan.
- Prefer preserving Linux layering boundaries (core vs transport vs frontend; object model vs view; lifetime vs sysfs/uevent).
- Avoid "implementation-driven APIs" that leak current simplifications (e.g., synchronous-only assumptions) into public interfaces.

Scope (tracked here):
- NVMe
- Virtio (core / virtqueue / virtio-pci transport)
- PCI / PCIe
- Block layer
- Filesystems (VFS / devtmpfs / ramfs / minixfs / procfs / sysfs)
- Memory management (mm/)
- Kernel core (kernel/)
- Kernel libraries (lib/)
- Architecture (arch/x86)
- Driver core (drivers/base)
- Timekeeping / clocksource
- Input / TTY / Console

## Cross-Cutting Alignment Rules

These rules apply to every subsystem section below.

### Execution Format (How To Make This Actionable)
- Each `Stage N` should have:
  - Work items: concrete edits with file/symbol pointers.
  - Acceptance: commands + observable behavior checks.
- Baseline acceptance (unless a stage says otherwise):
  - `make -j4`
  - `make smoke-512`

### Sysfs/Uevent Semantics
- Do not introduce custom event names or custom uevent key formats to convey state.
- Prefer Linux-standard uevent keys: `ACTION/DEVPATH/SUBSYSTEM/MODALIAS/DEVNAME/MAJOR/MINOR`.
- Sysfs must reflect Linux "real tree + view tree" semantics: `/sys/devices` is topology; `/sys/bus/*` and `/sys/class/*` are views.

Non-standard uevent/event inventory (Lite -> Linux):
- `ACTION=bar` / `ACTION=barfail` (PCI BAR allocation): DIFF -> replace with logs (BAR allocation is not a uevent action in Linux).
- `ACTION=enable` (PCI enable): DIFF -> replace with logs; device enable is internal to PCI core/driver.
- `ACTION=busnum` (bridge secondary bus assignment): DIFF -> replace with logs; topology is expressed via sysfs (and `struct pci_bus` in Linux).
- `ACTION=pciecap` (PCIe capability detected): DIFF -> replace with sysfs attributes or logs; Linux uevent does not use a separate action for this.
- `ACTION=nvme` (NVMe class detected): DIFF -> replace with driver binding + sysfs (`/sys/class/nvme`) and standard uevent fields.

Status:
- These non-standard `ACTION=` values have been removed from the codebase; Lite now emits uevents only via driver core actions (`add/remove/bind/unbind`) plus Linux-standard fields where applicable.

### Initcall Ordering
- Keep init ordering expressible in Linux initcall categories (`early/core/subsys/fs/device/late`), and avoid inventing new "ready" flags.
- If a subsystem needs "early availability", document the Linux mapping and why the initcall level is chosen.

### Lifetime/Reference Rules
- Every exported object must have a Linux-like lifetime story: who owns it, who references it, and when it is freed.
- If refcounting is simplified (e.g., non-atomic `kref`), mark it DIFF and do not present it as a Linux-strength guarantee.

### User ABI Stability
- Syscall numbers, sysfs paths, procfs file formats, and ioctl values are ABI. Do not change them without documenting Linux mapping and compatibility impact.
- Prefer aligning to Linux ABI surface rather than introducing Lite-only formats.

## NVMe

Linux mapping (vendored):
- `linux2.6/drivers/nvme/host/pci.c`: PCI transport, controller reset, queue bring-up
- `linux2.6/drivers/nvme/host/core.c`: core controller/namespace logic
- `linux2.6/drivers/nvme/host/nvme.h`: object model (`nvme_dev`, `nvme_ns`), lists, refs

Lite status:
- `drivers/nvme/nvme.c`: NVMe PCI transport + namespace/disk bring-up
- `drivers/nvme/nvme-core.c`: NVMe core controller object (`/sys/class/nvme/nvmeX`)
- `drivers/nvme/nvme_internal.h`: internal structs (`nvme_dev/nvme_ns`) and shared helpers

Key gaps (need convergence):
- Layering DIFF: core/transport/block glue are merged into one file; no Linux-like ctrl/ns boundary.
- Semantics DIFF: missing controller device object and `/sys/class/nvme/nvme0`-like entry; only gendisk is exposed.
- Flow/Lifetime DIFF: probe does synchronous init; failure reporting uses non-Linux-style custom uevent/event names; remove path is fragile.
- I/O model DIFF: synchronous single-queue request_fn + CQ polling (acceptable as a temporary simplification, but must not become public API shape).

Plan (staged):
- Stage 0: NVMe alignment report (Naming/Semantics/Flow-Lifetime mapping + risk list).
  - Work items:
    - Map current NVMe object lifetimes and sysfs exposure points in `drivers/nvme/nvme.c` to Linux `nvme_dev/nvme_ns` in `linux2.6/drivers/nvme/host/nvme.h`.
    - Identify all non-standard uevent/event usage and list Linux-standard replacements (log vs uevent fields).
  - Acceptance:
    - Document section updated with explicit DIFF/Why/Impact/Plan for each gap.
- Stage 1: controller object + class/sysfs semantics (do not change I/O model yet).
  - Work items:
    - Introduce controller device object (Linux concept: controller device under `/sys/class/nvme/`), parent of namespaces.
    - Ensure namespace gendisk (`nvme0n1`) has a stable parent relationship to controller object.
    - Remove/stop relying on custom uevent event names for failure signaling; use logs + standard fields where applicable.
  - Acceptance:
    - `/sys/class/nvme/nvme0` (or equivalent) exists and is the parent/anchor for namespaces.
    - Namespace disk still works with existing I/O path and tests.
- Stage 2: split core vs pci transport boundaries (`nvme_dev`/`nvme_ns` lifecycle + release paths).
  - Work items:
    - Split `drivers/nvme/nvme.c` into core vs pci transport compilation units (names aligned to Linux: `nvme-core.c`, `nvme-pci.c`).
    - Define explicit lifecycle ownership: who allocates/frees controller, namespaces, queues; ensure remove/unbind path is safe.
  - Acceptance:
    - Unbind/remove does not leak memory or leave sysfs nodes behind (basic smoke check by repeated probe/remove in test harness if available).
- Stage 3: align completion/interrupt model shape (poll as fallback; callback shape aligns to Linux).
  - Work items:
    - Introduce completion callback shape akin to Linux (even if internally still polls).
    - Isolate MSI/MSI-X enabling to only when used; avoid enabling IRQs without a corresponding IRQ completion path.
  - Acceptance:
    - No functional regression in existing NVMe I/O tests.
    - No "enable irq but never handle" behavior (trace/log review).
- Stage 4: smoke/regression: sysfs parent-child checks + basic read/write.
  - Work items:
    - Extend smoke to assert NVMe controller exists, namespace disk exists, and parent-child sysfs relationship is consistent.
  - Acceptance:
    - `make smoke-512` passes and includes the new NVMe sysfs checks.

NVMe alignment status (implemented):
- Stage 1: DONE (controller device object under `/sys/class/nvme`; namespace disk parented by controller).
- Stage 2: DONE (introduced `nvme-core.c`; transport/core split at compilation unit boundary).
- Stage 3: DONE (command submission split into submit vs completion poll; no MSI/MSI-X enabling without an IRQ completion path).
- Stage 4: DONE (smoke asserts `/sys/class/nvme/nvme0` and stable parent chain for `nvme0n1`).

## Virtio (Core / Virtqueue / Transport)

Linux mapping (vendored):
- `linux2.6/include/linux/virtio.h` and `linux2.6/drivers/virtio/virtio.c`: virtio core + bus glue
- `linux2.6/drivers/virtio/virtio_ring.c`: virtqueue/vring semantics
- `linux2.6/drivers/virtio/virtio_pci_common.c`, `virtio_pci_legacy.c`, `virtio_pci_modern.c`: virtio-pci transport layers

Lite status:
- `drivers/virtio/virtio.c`: minimal virtio bus + device/driver register
- `drivers/virtio/virtqueue.c`: minimal virtqueue helper (polling-centric)
- `drivers/virtio/virtio_pci.c`: virtio-pci transport (legacy + modern capability parsing; common/legacy/modern split inside file)

Key gaps (need convergence):
- Core semantics DIFF: feature negotiation helpers, device-ready/config-changed flow, vq lifecycle bookkeeping are incomplete.
- Virtqueue semantics DIFF: missing callback/enable_cb/disable_cb/get_buf/kick shape and SG/indirect/event-idx semantics.
- Transport DIFF: legacy-only; modern PCI capability model not supported; avoid frontend touching transport registers directly.
- External semantics DIFF: `/sys/bus/virtio` modalias/uevent binding semantics are incomplete.

Plan (staged):
- Stage 0: Virtio alignment report (explicit Linux mapping + DIFF list).
  - Work items:
    - Map Lite virtio core API in `drivers/virtio/virtio.c` to Linux `linux2.6/include/linux/virtio.h` and `linux2.6/drivers/virtio/virtio.c`.
    - Map Lite virtqueue functions in `drivers/virtio/virtqueue.c` to Linux `linux2.6/drivers/virtio/virtio_ring.c` semantics (list missing primitives).
  - Acceptance:
    - Each missing semantic has DIFF/Why/Impact/Plan recorded (no "TODO: later").
- Stage 1: core shape (device_ready/config_changed + feature/status helpers).
  - Work items:
    - Add Linux-like helpers (naming aligned) for status/feature negotiation and "device ready" flow.
    - Add a config-changed notification hook path (even if only used by a subset of devices).
  - Acceptance:
    - Frontend drivers do not access transport registers directly for core semantics.
- Stage 2: virtqueue base semantics (kick/get_buf + callback enable/disable; polling as fallback only).
  - Work items:
    - Implement `virtqueue_kick`, `virtqueue_get_buf`, `virtqueue_enable_cb`, `virtqueue_disable_cb` semantics consistent with Linux naming/behavior.
    - Ensure polling is an internal strategy, not a requirement of the virtqueue API.
  - Acceptance:
    - Existing virtio frontends still work; virtqueue API is stable and Linux-shaped.
- Stage 3: transport split (legacy/modern/common; modern capability parsing).
  - Work items:
    - Split `drivers/virtio/virtio_pci.c` into legacy/modern/common layers (Linux naming and boundary).
    - Implement modern PCI capability parsing, with clear fallback to legacy for unsupported platforms.
  - Acceptance:
    - Legacy path remains working; modern path is feature-gated and does not regress.
- Stage 4: external semantics (modalias/uevent + driver bind path).
  - Work items:
    - Ensure `/sys/bus/virtio` and modalias fields exist and are used by driver binding logic (no custom event names).
  - Acceptance:
    - Bind/unbind uses driver core semantics; uevent fields are Linux-standard.
- Stage 5: smoke/regression: virtio framework semantics tests (bind/unbind + vq create/destroy + frontend attach/detach).
  - Work items:
    - Add smoke assertions for virtio bus/driver binding and virtqueue creation/destruction stability.
  - Acceptance:
    - `make smoke-512` passes with added virtio framework checks.

Virtio alignment status (implemented):
- Stage 1: DONE (status/feature helpers + device_ready/config_changed hooks).
- Stage 2: DONE (virtqueue_kick/get_buf/enable_cb/disable_cb; add_buf does not auto-notify).
- Stage 3: DONE (modern virtio-pci capability parsing; legacy fallback retained; modern BAR MMIO regions are mapped through `ioremap()` semantics instead of lowmem directmap).
- Stage 4: DONE (virtio modalias aligned: `virtio:d%08Xv%08X`; sysfs modalias exposed on virtio bus).
- Stage 5: DONE (smoke asserts virtio bus sysfs + virtio_scsi driver name).

## PCI / PCIe

Linux mapping (vendored):
- `linux2.6/drivers/pci/probe.c`: enumeration, bridge/bus objects, resource windows, `pcibus_class`
- `linux2.6/drivers/pci/pci-driver.c`: driver core glue, sysfs new_id/remove_id, reprobe/attach

Lite status:
- `drivers/pci/pci.c`: minimal enumeration + config space access + simplified BAR placement + driver glue
- `drivers/pci/pcie/pcie.c`: minimal PCIe capability presence tracking

Key gaps (need convergence):
- Resource model DIFF: fixed global "BAR allocator" window is not Linux-like; Linux uses host bridge windows + `struct resource` tree.
- Object model DIFF: missing `struct pci_bus` as a first-class object in device model; topology is kept as ad-hoc arrays.
- External semantics DIFF: Lite previously used non-standard uevent `ACTION=` values to signal PCI state (now removed); remaining DIFF is the global `/sys/kernel/uevent` buffer semantics vs Linux per-device uevent semantics.
- PCIe DIFF: capability parsing exists only as a tag; must integrate into PCI core capability framework and provide Linux-like semantic hooks.

Plan (staged):
- Stage 0: PCI/PCIe alignment report (explicit mapping + DIFF list).
  - Work items:
    - Map `drivers/pci/pci.c` enumeration, BAR placement, and driver binding to Linux `linux2.6/drivers/pci/probe.c` and `pci-driver.c`.
    - Identify all non-Linux event signaling and list standard alternatives.
  - Acceptance:
    - Document explicitly states how BAR windows are sourced and what is currently DIFF.
- Stage 1: introduce minimal `pci_bus` object model + `pcibus_class` (device-model first).
  - Work items:
    - Add a minimal `struct pci_bus` and register bus objects in the device model (Linux concept).
    - Replace ad-hoc parent arrays with explicit bus/bridge linkage.
  - Acceptance:
    - Topology relationships are representable in sysfs without custom hacks.
- Stage 2: introduce Linux-style resource windows (`struct resource`) for root bus/bridges; remove blind BAR allocation.
  - Work items:
    - Represent root bus windows as `struct resource` and allocate BARs within those windows.
    - Track bridge windows and propagate resources downstream (even if simplified for a single root bus).
  - Acceptance:
    - BAR placement no longer uses a global allocator cursor that can overlap RAM.
- Stage 3: external semantics cleanup (standard uevent fields; bind via driver core; no custom event names).
  - Work items:
    - Remove custom uevent/event names used to report PCI state; rely on standard fields and logs.
  - Acceptance:
    - Driver binding does not depend on custom event strings.
- Stage 4: PCIe capability semantic boundaries (MSI/MSI-X, link/port hooks) with Linux terminology.
  - Work items:
    - Integrate PCIe capability parsing into PCI core and provide Linux-like helpers (not just an offset tag).
  - Acceptance:
    - PCIe capability usage is structured and does not leak transport details into drivers.

PCI/PCIe alignment status (implemented):
- Stage 1: DONE (introduced minimal `struct pci_bus` and removed ad-hoc per-bus arrays; bus objects are created and registered under `pcibus_class`).
- Stage 2: DONE (introduced Linux-style IO/MEM/PREFETCH window modeling via `struct resource` and allocates BARs within bus windows; removed global BAR allocator cursors).
- Stage 3: DONE (removed custom PCI/PCIe uevent `ACTION=` values; rely on logs + sysfs + standard uevent fields).
- Stage 4: DONE (added Linux-like PCIe capability helpers and moved drivers to query PCIe presence via helpers, not by peeking at raw offset tags).

## Block Layer

Linux mapping (vendored):
- `linux2.6/block/blk-core.c`: request queue, plug/unplug, completion semantics, throttling
- `linux2.6/fs/block_dev.c`: bdev open/close, `bd_inode` binding, partitions/rescan
- `linux2.6/block/genhd.c`: gendisk/add_disk and sysfs relationships

Lite status:
- `block/blk-core.c`: minimal single-queue request processing
- `block/blkdev.c`: minimal gendisk/add_disk and `/sys/class/block` view; simplified bdev creation
- `fs/block_dev.c`: blockdev inode + pagecache mapping to block_device_read/write

Key gaps (need convergence):
- Lifetime DIFF: Linux binds `block_device` in open path; Lite folds multiple lifecycle stages into `add_disk()`.
- Queue semantics DIFF: Linux has rich concurrency/backpressure semantics; Lite is synchronous (allowed as simplification, but must keep Linux concept boundaries).
- External semantics DIFF: sysfs parent-child and attributes are minimal; must align relationships, not just names.

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - `gendisk` ownership: Lite matches Linux shape (per-disk `request_queue` on `gendisk`), see `include/linux/blkdev.h` + `block/blk-core.c`.
  - I/O submission path: Lite has `submit_bio()` -> `generic_make_request()` -> `request_fn()` sync execution (single-queue), see `block/blk-core.c`.
  - bdev identity: Lite has a simplified whole-disk `block_device` keyed only by `dev_t` (no partitions), see `block/blkdev.c` (`bdev_map[]`).
- DIFF (lifetime):
  - Lite creates `struct block_device` lazily on first `bdget()`/`bdget_disk()` (not as a side-effect of `add_disk()`).
  - Linux ties many bdev lifetimes to open/lookup paths (e.g., `bdget` / `blkdev_get` / `bdput`) and binds `bd_inode` accordingly (not during `add_disk()`).
- DIFF (external semantics):
  - Lite creates the `/sys/class/block/<disk>` device at disk registration, but bdev inode/pagecache is created lazily (devtmpfs node creation or first bdev lookup).
  - Linux separates sysfs disk object registration from bdev open/close semantics more cleanly.
- Decision (Stage 0):
  - Scope: keep **non-blk-mq** single-queue model for now (teaching kernel). This must remain an internal simplification and must not force drivers to depend on synchronous queue execution.

Plan (staged):
- Stage 0: Block alignment report (decide long-term non-mq vs blk-mq convergence).
  - Work items:
    - Map Lite `block/blk-core.c` and `block/blkdev.c` lifetimes to Linux `linux2.6/block/blk-core.c` and `linux2.6/block/genhd.c`.
    - Decide and document whether blk-mq is in-scope; if not, document long-term DIFF boundaries.
  - Acceptance:
    - A clear statement exists: "non-mq only" vs "eventual blk-mq", with impact.
- Stage 1: move bdev lifecycle closer to Linux open/bind flow; make release paths explicit.
  - Work items:
    - Stop creating bdev as a side-effect of `add_disk()`; move binding toward open path (`fs/block_dev.c`).
    - Make `del_gendisk/put_disk`-like semantics explicit and safe.
  - Acceptance:
    - Repeated open/close does not leak bdev objects; remove path cleans sysfs and in-kernel objects.
- Stage 2: align queue run/completion semantics boundaries (even if simplified internally).
  - Work items:
    - Keep API boundaries Linux-like (`request_queue`, request completion), even if internally synchronous.
    - Add minimal recursion protection/backpressure knobs that do not leak "always sync" into drivers.
  - Acceptance:
    - Existing block drivers compile unchanged; no ad-hoc driver-specific queue assumptions appear.
- Stage 3: sysfs semantics + smoke checks tied to lifecycle.
  - Work items:
    - Add smoke checks for `/sys/class/block/<disk>` basic attributes and parent-child relationships.
  - Acceptance:
    - `make smoke-512` passes with added block sysfs assertions.

Block alignment status (implemented):
- Stage 0: DONE (documented non-mq scope + bdev/add_disk lifetime DIFFs).
- Stage 1: DONE (bdev is lazy but now has Linux-like `bdget`/`bdput` lifetime refs independent of openers; inode holds a bdev ref; `del_gendisk()` drops inode/pagecache and unregisters bdev safely).
- Stage 2: DONE (`request_queue` tracks `nr_requests/queued/in_flight`; queue-full submission applies minimal backpressure (drain-once) only when interrupts are enabled; recursion is bounded by `q->running` so drivers do not rely on "always sync").
- Stage 3: DONE (`make smoke-512` checks `/sys/class/block/<disk>/{type,parent,size,dev,stat}` for ram0 and checks nvme disk parent/type/size/dev when present).

## Filesystems (VFS / devtmpfs / ramfs / minixfs / procfs / sysfs)

### VFS + Namespace + Dcache

Linux mapping (vendored):
- `linux2.6/fs/namei.c`: path lookup, `nameidata`, symlink following, permission checks
- `linux2.6/fs/namespace.c`: mount namespace, mountpoint traversal, `vfsmount` lifetime
- `linux2.6/fs/dcache.c`: dcache, refcounting, negative dentries, lookup rules
- `linux2.6/fs/inode.c`, `linux2.6/fs/file_table.c`, `linux2.6/fs/open.c`: inode/file lifecycle

Lite status:
- Path lookup: `fs/namei.c` (`path_walk()` is string-driven; minimal symlink following)
- Mounting: `fs/namespace.c` (`register_filesystem()`, `vfs_get_sb_single()`, `vfs_mount()`)
- Dentry/inode basics: `fs/dentry.c`, `fs/inode.c`, plus `fs/file.c`, `fs/open.c`

Key gaps (need convergence):
- Path semantics DIFF: lookup is string-centric and partially coupled to `task_get_cwd()` being a string.
  - Linux uses `struct path` (dentry+vfsmount) and resolves relative paths from `pwd` without rebuilding strings.
- Mount semantics DIFF: `vfs_mount()` links the mounted root by manually rewriting `mount_root->parent` to allow `cd ..` escaping.
  - This breaks Linux mountpoint semantics (Linux keeps mount root and mountpoint distinct; `..` at mount root is controlled by mount topology, not by rewriting dentry parent).
- Symlink semantics DIFF: only absolute symlink targets are supported and symlink "read" is overloaded to return link target.
  - Linux symlinks support relative targets and use `->follow_link` / `get_link` style semantics.
- Dcache semantics DIFF: dentries are never freed; no negative dentry; no hashing; minimal refcounting.
  - This is acceptable for a teaching kernel but must be treated as DIFF and must not leak into public API assumptions.
- Superblock semantics DIFF: only `get_sb_single` exists; no `sget` / mount options parsing; `kill_sb` is unused.

Plan (staged, not started yet):
- Stage 0: VFS alignment report (map Lite call chains to Linux: lookup/open/mount/dcache lifetimes).
- Stage 1: move `cwd` to dentry-based semantics (stop using path strings as the source of truth).
- Stage 2: redesign mountpoint traversal to Linux-like topology (no parent rewriting; explicit mountpoint/root linkage).
- Stage 3: strengthen dcache rules (negative dentries, refcounting/freeing policy, basic hashing).
- Stage 4: symlink semantics (relative targets, loop detection consistent with Linux behavior).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Path walk: Linux `namei.c` uses `struct nameidata` and dentry cache; Lite uses `path_walk()` over a `dentry` tree with a fallback `finddir_fs()` call.
  - CWD/PWD: Linux tracks `pwd` as `struct path` (dentry+vfsmount); Lite tracks `current->fs.pwd` as a dentry pointer (and older code/comments still reference string cwd).
  - Mount topology: Linux keeps mountpoint and mounted root distinct via `vfsmount` topology; Lite stores `dentry->mount` and enters mounted root in `path_walk()` via `if (curr->mount) curr = curr->mount->root`.
  - Dcache: Linux has hashed dentries, negative dentries, refcount and reclaim; Lite keeps a simple children linked list and never frees dentries (`vfs_dentry_put()` only decrements).
- DIFF (mount semantics):
  - Lite rewrites `mount_root->parent = d->parent` to allow `cd ..` to escape mounts.
  - Why: simple teaching topology without full mountpoint/root separation.
  - Impact: `..` at mount root does not follow Linux rules; userspace path semantics can diverge and can escape in unexpected ways.
  - Plan: Stage 2 removes parent rewriting and introduces explicit mountpoint/root linkage (Linux-shaped).
- DIFF (lookup + cwd semantics):
  - Lite is dentry-based for relative paths, but still lacks Linux `struct path` semantics, and does not enforce mount namespace boundaries.
  - Why: no full `vfsmount` + refcount model yet.
  - Impact: correctness gaps for relative paths across mounts; hard to implement `chroot`-like semantics later.
  - Plan: Stage 1 introduces dentry-based cwd as the only truth (remove any remaining string-based cwd assumptions) and then Stage 2 introduces mount topology rules.
- DIFF (symlink semantics):
  - Lite only follows absolute symlink targets and overloads `->read` to return the link target.
  - Why: sufficient for sysfs absolute links; simplifies implementation.
  - Impact: relative symlinks and symlink loop detection differ from Linux.
  - Plan: Stage 4 introduces Linux-like `get_link` semantics and relative target handling.
- DIFF (dcache lifetime):
  - Lite does not free dentries and has no negative dentries/hashing.
  - Why: teaching kernel simplification.
  - Impact: memory growth and lookup behavior differs; cannot rely on Linux dcache performance/eviction semantics.
  - Plan: Stage 3 adds a minimal refcount/free policy + optional hashing/negative dentry subset.

VFS alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + DIFF/Why/Impact/Plan for mount/cwd/symlink/dcache semantics).
- Stage 1: DONE (`cwd` uses `current->fs.pwd` dentry as the single source of truth; relative `path_walk()` starts from dentry, not rebuilt strings).
- Stage 2: DONE (mount topology uses explicit `vfsmount.mountpoint/parent` linkage; `..` at mounted root escapes via mount topology, not by rewriting dentry parents).
- Stage 3: DONE (dcache supports negative dentries, `unlink/rmdir` invalidate cached dentries, and detached dentries are freed on last put).
- Stage 4: DONE (symlink supports relative targets + loop detection; initramfs can unpack symlink entries; smoke validates relative link and loop rejection).

### devtmpfs

Linux mapping (vendored):
- `linux2.6/drivers/base/devtmpfs.c`: kernel-maintained tmpfs-based `/dev`, created and updated by driver core

Lite status:
- `fs/devtmpfs/devtmpfs.c`: devtmpfs is implemented as a standalone pseudo filesystem with an in-kernel node array.
- Device nodes are materialized by scanning `registered_device_*()` and mapping devnodes to inodes ad-hoc.

Key gaps (need convergence):
- Layering DIFF: Linux devtmpfs is a tmpfs/ramfs-based mount driven by driver core; Lite devtmpfs is a custom FS with bespoke node management.
- Semantics DIFF: node permissions/uid/gid and block-vs-char classification are simplified and partially hardcoded.
- Flow/Lifetime DIFF: Linux supports async requests and consistent create/remove semantics; Lite rebuilds from the device list at mount and uses array delete.

Plan (staged, not started yet):
- Stage 0: devtmpfs alignment report (identify required Linux semantics and what can remain simplified).
- Stage 1: converge to Linux concept: devtmpfs as "kernel-managed tmpfs-like /dev" driven by driver core notifications.
- Stage 2: make node metadata come from `device_get_devnode()`-style hooks (mode/uid/gid), not from hardcoded device-type checks.

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux devtmpfs (`linux2.6/drivers/base/devtmpfs.c`) is driver-core-driven and uses tmpfs/ramfs to materialize nodes.
  - Lite devtmpfs is a standalone pseudo filesystem (`fs/devtmpfs/devtmpfs.c`) that maintains an in-kernel node array and creates inodes directly.
- DIFF (layering):
  - Lite does not reuse ramfs/tmpfs; it builds `/dev` from `registered_device_*()` at mount time and then updates via `devtmpfs_register_device()/devtmpfs_unregister_device()`.
  - Why: avoid implementing full tmpfs + device core integration in a teaching kernel.
  - Impact: create/remove ordering and inode lifetime semantics differ from Linux; metadata (mode/uid/gid) is simplified.
  - Plan: Stage 1 turns devtmpfs into a driver-core-managed view (even if still backed by a simple in-memory inode store).
- DIFF (block device node creation):
  - Lite creates a bdev inode lazily when materializing `/dev/<disk>` (via `bdget_disk()` + `blockdev_inode_create()`).
  - Why: keep bdev lifetime closer to open path and avoid eager `add_disk()` side effects.
  - Impact: `/dev` node availability depends on devtmpfs mount and device list scan timing.
  - Plan: Stage 2 pulls node metadata from a Linux-like `device_get_devnode()` + mode hook, and keeps bdev lifetime independent from devtmpfs details.

devtmpfs alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + DIFF/Why/Impact/Plan).
- Stage 1: DONE (device core drives `/dev` updates via `devtmpfs_register_device()/devtmpfs_unregister_device()`, not just mount-time scan).
- Stage 2: DONE (node metadata comes from a Linux-like `device_get_devnode(dev, &mode, &uid, &gid)` hook; devtmpfs uses a generic chrdev inode dispatch by `devt` for tty/console).

### ramfs

Linux mapping (vendored):
- `linux2.6/fs/ramfs/inode.c`: ramfs built on VFS caches; inode creation via `new_inode()` and generic aops

Lite status:
- `fs/ramfs/ramfs.c`: minimal ramfs using generic file read/write and a simple in-memory dentry tree

Key gaps (need convergence):
- Semantics DIFF: inode creation and aops/mapping behaviors are simplified; directory link counts and timestamps are not Linux-like.
- Dcache DIFF: relies on Lite dentry implementation rather than Linux-style dcache semantics.

Plan (staged, not started yet):
- Stage 0: ramfs alignment report (what semantics are relied on by other subsystems, e.g., rootfs).
- Stage 1: align ramfs inode creation/lifetime to Linux idioms (even if caches remain simplified).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux ramfs is a simple in-memory filesystem but still uses core VFS idioms: `new_inode()`, `address_space` + aops, and standard directory operations (`linux2.6/fs/ramfs/inode.c`).
  - Lite ramfs is used as the rootfs via `vfs_mount_rootfs("ramfs")` and relies on Lite dentry+inode primitives plus generic read/write helpers (`fs/ramfs/ramfs.c`).
- DIFF (inode + mapping semantics):
  - Lite inode/mapping behaviors are simplified (mode/uid/gid are basic; timestamps/link counts are not Linux-like).
  - Why: keep rootfs minimal for a teaching kernel.
  - Impact: tools and subsystems that rely on Linux inode metadata semantics can diverge; subtle permission/visibility differences may appear.
  - Plan: Stage 1 aligns inode allocation/lifetime to Linux idioms even if caching remains simplified.
- DIFF (dcache dependence):
  - Lite ramfs correctness/perf is tightly coupled to Lite dentry rules (no negative dentries, no reclaim).
  - Why: Lite dcache is intentionally minimal.
  - Impact: ramfs directory lookup behavior differs from Linux under churn; memory growth is unbounded.
  - Plan: Stage 3 in VFS section introduces minimal dcache refcount/free policy, which ramfs should adopt.

ramfs alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + DIFF/Why/Impact/Plan; identified rootfs dependency).

### minixfs

Linux mapping (vendored):
- `linux2.6/fs/minix/*`: minix inode/dir/file logic, buffer cache and inode cache integration

Lite status:
- `fs/minixfs/minixfs.c`: minimal minix reader/writer (V1-style), mounts by reading super/inode tables and pre-populates root children at mount time.

Key gaps (need convergence):
- Lookup semantics DIFF: pre-populating dentries at mount time is not Linux VFS behavior; Linux relies on lookup + dcache population on demand.
- Data/metadata lifetime DIFF: no inode cache, no writeback integration, no `super_operations` lifecycle methods (`put_super`, `evict_inode`).
- Feature scope DIFF: only direct zones; indirect blocks and more complete minix variants are not supported.

Plan (staged, not started yet):
- Stage 0: minixfs alignment report (explicitly mark supported on-disk format and limitations).
- Stage 1: move toward Linux lookup model (populate dentries on demand, not via mount-time directory scan).
- Stage 2: add minimal super/inode lifecycle hooks (even if writeback remains simplified).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux minixfs integrates with buffer cache and inode cache; directories are populated on demand via lookup + dcache (`linux2.6/fs/minix/*`).
  - Lite minixfs mounts by reading super/inode tables and eagerly pre-populates dentries for the root directory at mount time (`fs/minixfs/minixfs.c`).
- DIFF (lookup model):
  - Lite does mount-time directory scanning and dentry pre-population, rather than Linux on-demand lookup.
  - Why: simplify implementation and avoid building a full dcache/lookup path first.
  - Impact: mount-time cost grows with directory size; runtime lookup semantics and cache behavior differ from Linux.
  - Plan: Stage 1 moves to Linux-like lookup (populate dentries on demand).
- DIFF (lifecycle hooks):
  - Lite does not implement Linux-like `super_operations` and inode eviction/writeback lifetimes.
  - Why: buffer cache/writeback are simplified.
  - Impact: unmount/remount semantics and dirty data guarantees differ.
  - Plan: Stage 2 adds minimal super/inode lifecycle hooks, matching Linux naming and concept boundaries.
- DIFF (format scope):
  - Lite supports only a limited minix on-disk variant (documented as V1-style) and omits indirect blocks.
  - Why: keep scope small.
  - Impact: many minix images will not mount; file size limits differ.
  - Plan: Stage 0 requires explicitly recording supported format + limitations (this section); Stage 1/2 do not expand format unless required.

minixfs alignment status (implemented):
- Stage 0: DONE (documented on-disk scope + key DIFFs + staged plan).

### procfs

Linux mapping (vendored):
- `linux2.6/fs/proc/*` (e.g., `root.c`, `base.c`, `array.c`): procfs tree, per-pid entries, namespaces, mount options, seq_file usage

Lite status:
- `fs/procfs/procfs.c` plus helper files under `fs/procfs/`: minimal procfs with a fixed set of inodes and a bounded per-pid table.

Key gaps (need convergence):
- Object model DIFF: fixed-size `PROC_PID_MAX` table is not Linux-like; procfs entries are largely static and lack `proc_dir_entry`-style dynamism.
- Semantics DIFF: mount options, namespaces (`pid_namespace`), and hidepid semantics are absent.
- API shape DIFF: no `seq_file` abstraction; content generation uses fixed buffers per read.

Plan (staged, not started yet):
- Stage 0: procfs alignment report (map which `/proc/*` files exist and their Linux equivalents).
- Stage 1: introduce a minimal `proc_dir_entry`-style object model to build the tree without static inode blobs.
- Stage 2: add `seq_file`-like iteration for dynamic data to avoid fixed buffer limits.

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - procfs is a virtual filesystem exposing kernel state. Linux uses `proc_dir_entry` to build a dynamic tree, and commonly uses `seq_file` for iterative reads.
  - Lite procfs is implemented as a pseudo filesystem with a fixed set of `struct inode` objects and read handlers that render into fixed buffers (`fs/procfs/procfs.c` + helpers under `fs/procfs/`).
- Coverage (what exists in Lite):
  - Global entries: `/proc/tasks`, `/proc/sched`, `/proc/irq`, `/proc/meminfo`, `/proc/iomem`, `/proc/cow`, `/proc/pfault`, `/proc/vmscan`, `/proc/writeback`, `/proc/pagecache`, `/proc/blockstats`, `/proc/diskstats`, `/proc/mounts`.
  - Per-PID subset (bounded table): `/proc/<pid>/{maps,stat,cmdline,status,cwd,fd/*}`.
- DIFF (object model):
  - Lite uses a fixed `PROC_PID_MAX` table and largely static inode objects; there is no Linux-like `proc_dir_entry` tree construction.
  - Why: keep procfs small and deterministic for a teaching kernel.
  - Impact: procfs cannot represent dynamic proc entries well; PID scalability and correctness diverge from Linux.
  - Plan: Stage 1 introduces a minimal `proc_dir_entry`-style object model (even if still bounded).
- DIFF (API shape and read semantics):
  - Lite does not implement `seq_file`; reads are served from fixed temporary buffers.
  - Why: avoid iterator/lseek corner cases early on.
  - Impact: large proc outputs can truncate; semantics differ for partial reads and long iteration.
  - Plan: Stage 2 adds a `seq_file`-like iterator model for dynamic data sources.
- DIFF (symlink/cwd semantics):
  - Linux represents `/proc/<pid>/cwd` as a symlink; Lite exposes it as a file-like inode and currently returns a trivial placeholder.
  - Why: Lite VFS and symlink model are simplified.
  - Impact: tools expecting Linux `/proc/<pid>/cwd` symlink behavior will not work correctly.
  - Plan: align procfs `cwd` exposure with the Stage 4 symlink work in the VFS section.

procfs alignment status (implemented):
- Stage 0: DONE (documented what exists in Lite procfs and the major Linux DIFFs + plan).
- Stage 1: DONE (introduced a minimal proc_dir_entry-style child table for `/proc` root; PID subdirs remain bounded but are enumerated via the same lookup/readdir path).
- Stage 2: DONE (migrated `/proc/tasks` and `/proc/mounts` to seq_file-like streaming generation to avoid fixed buffer limits; `/proc/<pid>/cwd` is now a symlink and resolves to the task cwd).

### sysfs

Linux mapping (vendored):
- `linux2.6/fs/sysfs/*` (mount, dir, file, symlink): sysfs implemented on kernfs and kobject model, namespace-aware mount

Lite status:
- `fs/sysfs/sysfs.c`: sysfs implemented as a pseudo filesystem with a Linux-like `sysfs_dirent` cache bound to `kobject`.

Key gaps (need convergence):
- Layering DIFF: Linux sysfs is built on kernfs; Lite implements sysfs directly in terms of Lite inode/dentry with partial caching.
- Lifetime DIFF: sysfs node lifecycle should be strictly tied to kobject lifetime and reference counting; Lite materializes dentries opportunistically.
- External semantics DIFF: symlink and attribute file semantics are simplified; ensure they match Linux expectations (permissions/visibility and link targets).

Plan (staged, not started yet):
- Stage 0: sysfs alignment report (map Lite sysfs APIs to Linux sysfs/kernfs concepts).
- Stage 1: tighten kobject<->sysfs node lifetime rules (no stale dentries; explicit detach semantics).
- Stage 2: converge symlink + attribute read/write semantics to Linux behavior (including relative link targets where required).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux sysfs is the user-visible projection of the kobject model (`kobject`/`kset`/`ktype` + attribute groups), implemented on kernfs (`linux2.6/fs/sysfs/*`).
  - Lite sysfs is a pseudo filesystem that caches a Linux-like `sysfs_dirent` per kobject and materializes dentry/inode nodes on demand (`fs/sysfs/sysfs.c`).
- Implemented semantics (Lite):
  - kobject directory creation: `sysfs_create_dir()` binds a `sysfs_dirent` to `kobj->sd` and materializes child dentries if the parent is already materialized.
  - attribute visibility/mode: `sysfs_kobj_attr_mode()` consults `ktype->default_groups/default_attrs` and `device` group sets (dev/type/class/bus) to decide effective mode.
  - symlink nodes: `sysfs_create_link()` stores an absolute sysfs path string and exposes it via `read` on a symlink inode (no relative link target handling).
- DIFF (layering):
  - Lite sysfs is not kernfs-backed and is implemented directly on Lite VFS structures.
  - Why: kernfs is out-of-scope; keep sysfs minimal.
  - Impact: Linux sysfs/kernfs edge semantics (refcounts, rename, namespace mounts) are not matched.
  - Plan: Stage 1 focuses on lifetime correctness and detach semantics first (no stale dentries), without introducing kernfs.
- DIFF (lifetime and refcount boundaries):
  - Linux sysfs lifetime is strictly tied to kobject refcounting; Lite caches `kobj->sd` and may keep inode/dentry pointers materialized across operations.
  - Why: simplified memory management and lack of kernfs refcount model.
  - Impact: risk of stale nodes if kobject lifetimes or detach ordering changes; must keep sysfs robust in early boot.
  - Plan: Stage 1 tightens kobject<->sysfs detach rules, making "remove" paths consistently invalidate and detach dentries.
- DIFF (symlink semantics):
  - Lite symlink uses `read` to return the stored absolute target path string.
  - Why: enough for sysfs internal links in Lite (and keeps sysfs implementation small).
  - Impact: behavior differs from Linux `readlink()` semantics and relative target handling.
  - Plan: Stage 2 aligns symlink semantics with the VFS symlink plan (relative targets where required).

sysfs alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + Lite sysfs object model + major DIFFs).
- Stage 1: DONE (sysfs create/remove paths clear negative dentry cache and detach cached dentries to avoid stale reachability after unbind/remove).
- Stage 2: PARTIAL (attribute store trims trailing newline and enforces offset=0; named `attribute_group.name` entries now materialize as real subdirectories; sysfs relative link targets remain deferred due to reachability regressions).

## Memory Management (mm/)

Linux mapping (vendored):
- Core VM: `linux2.6/mm/memory.c`, `linux2.6/mm/mmap.c`
- Page allocator / zones: `linux2.6/mm/page_alloc.c`, `linux2.6/mm/mmzone.c`, `linux2.6/mm/bootmem.c`
- Reclaim / swap: `linux2.6/mm/vmscan.c`, `linux2.6/mm/swap.c`, `linux2.6/mm/swap_state.c`
- Reverse mapping: `linux2.6/mm/rmap.c`
- Slab: `linux2.6/mm/slab.c` (and `slab_common.c`, `slub.c`, `slob.c`)
- Vmalloc: `linux2.6/mm/vmalloc.c`
- Page cache / writeback: `linux2.6/mm/filemap.c`, `linux2.6/mm/page-writeback.c`, `linux2.6/mm/truncate.c`

Lite status (mm/):
- `mm/bootmem.c`: early allocator + e820-like region handling
- `mm/mmzone.c`: `pglist_data` + two zones (`ZONE_DMA`, `ZONE_NORMAL`) and simple watermarks
- `mm/page_alloc.c`: buddy allocator with watermark checks; direct call into reclaim
- `mm/memory.c`: page table manipulation, page fault handling, COW, and user mapping helpers
- `mm/mmap.c`: VMA list management (`mm_struct->mmap`), `dup_mm()` and teardown logic
- `mm/rmap.c`: minimal reverse mapping (`page->map_mm/map_vaddr` + per-page list)
- `mm/vmscan.c`: minimal reclaim (`page_cache_reclaim_one()` then swap-out of a mapped anon page)
- `mm/swap.c`: in-memory "swap slots" (no swap device), keyed by (mm, vaddr)
- `mm/vmalloc.c`: simple linear vmalloc/ioremap and vfree list
- `mm/slab.c`: fixed-size caches + per-page freelist; `kmalloc/kfree` built on it
- `mm/filemap.c`: simple page cache per `address_space` with dirty tracking and a basic writeback hook

Key gaps (need convergence, do not paper over with ad-hoc behavior):

### Page allocator / zones / bootmem
- Semantics DIFF: buddy lists are a single linked list per order without per-cpu page lists (pcp) and without proper zonelist/nodemask semantics.
- Semantics DIFF: watermarks exist but reclaim triggering is direct and synchronous; Linux separates allocator fast paths from reclaim via proper reclaim control loops.
- Flow/Lifetime DIFF: bootmem and page allocator boundaries are simplified; Linux evolves from bootmem/memblock to buddy with clear handoff semantics.

Plan (staged, not started yet):
- Stage 0: allocator alignment report (map Lite alloc/free, zone watermarks, and bootmem handoff to Linux equivalents).
- Stage 1: tighten zonelist + GFP semantics boundaries (avoid "implementation-driven" fallbacks that hide failures).
- Stage 2: introduce minimal per-zone reclaim control (scan control shape like Linux `scan_control`, even if simplified).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux has a clear early allocator -> buddy handoff, and separates zones, zonelists, watermarks, and reclaim decisions (`linux2.6/mm/bootmem.c`, `linux2.6/mm/page_alloc.c`, `linux2.6/mm/mmzone.c`).
  - Lite provides `bootmem_alloc()` as an early allocator and transitions to a buddy allocator (`mm/bootmem.c`, `mm/page_alloc.c`), with a minimal `pglist_data` + two zones (`ZONE_DMA`, `ZONE_NORMAL`) (`mm/mmzone.c`).
- Implemented semantics (Lite):
  - Early: before `buddy_ready`, `__alloc_pages_nodemask()` allocates via `bootmem_alloc()` and returns a `struct page *` for the PFN.
  - After buddy init: `__alloc_pages_nodemask()` chooses a zonelist (`dma_zonelist` or `contig_zonelist`), enforces `WMARK_{HIGH,LOW,MIN}` in three passes, and triggers reclaim via `wakeup_kswapd()` and `try_to_free_pages()`.
  - Address discipline: `free_pages()` asserts the input is a physical address (panic if `addr >= PAGE_OFFSET`) and page-aligned.
- DIFF (zonelist + nodemask scope):
  - Lite ignores nodemask and uses two fixed zonelists (DMA-only vs contig).
  - Why: single-node teaching kernel.
  - Impact: allocation behavior differs from Linux under pressure; no NUMA/topology knobs.
  - Plan: Stage 1 tightens GFP/zonelist boundaries and documents what is intentionally unsupported.
- DIFF (reclaim coupling):
  - Lite triggers reclaim synchronously in the allocator slow path (`try_to_free_pages()` is called directly).
  - Why: avoid implementing full kswapd scheduling and LRU.
  - Impact: allocator latency can spike; semantics differ from Linux separation of fast paths and reclaim control.
  - Plan: Stage 2 introduces a Linux-shaped reclaim control boundary (even if simplified) to avoid ad-hoc growth.

allocator alignment status (implemented):
- Stage 0: DONE (documented bootmem->buddy handoff, zonelist/watermark behavior, and reclaim coupling DIFFs).
- Stage 1: DONE (generic `alloc_pages()` now honors `gfp_mask`-selected zonelists instead of forcing the contig zonelist, so `GFP_DMA` remains a real allocation boundary in the common path).
- Stage 2: DONE (allocator slow path now enters reclaim through a minimal `scan_control`-shaped boundary via `try_to_free_pages_sc()` instead of calling an unparameterized reclaim body directly).

### Page tables / fault / COW (mm/memory.c)
- Semantics DIFF: page fault statistics and basic COW exist, but Linux has a structured fault path (`do_page_fault` -> `handle_mm_fault`) with VMA-based permission checks and distinct fault types.
- Flow/Lifetime DIFF: user mappings, unmap, and teardown should follow Linux-like semantics (unmap ranges, free page tables, maintain mapcounts/rmap).

Plan (staged, not started yet):
- Stage 0: fault/COW alignment report (map Lite fault path to Linux `handle_mm_fault` model).
- Stage 1: make VMA permission checks the single source of truth for PTE flags (avoid ad-hoc user/write permission fixes).
- Stage 2: refine teardown/unmap logic and ensure rmap/mapcount stays consistent under fork/COW.

fault/COW alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + DIFF/Why/Impact/Plan).
- Stage 1: DONE (introduced Linux-shaped `handle_mm_fault()` boundary and centralized user-fault permission/COW/swap-in decisions).
- Stage 2: DONE (introduced Linux-like `zap_page_range()` boundary for unmap/teardown and now reclaim empty user page tables during munmap/mremap shrink/mm teardown).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux fault entry: `do_page_fault()` (arch) decodes the error code and then faults via `handle_mm_fault()` / `handle_pte_fault()` after locating the covering VMA (`linux2.6/mm/memory.c` + arch handler).
  - Linux COW: write-protect faults are resolved by `do_wp_page()`-style logic: upgrade if unshared, else copy; correctness relies on per-page mapcounts and anon_vma/rmap bookkeeping.
  - Lite fault entry: `do_page_fault()` decodes CR2 + error bits, classifies null/kernel/out-of-range, and either allocates a zero page, swaps in, or resolves COW (`mm/memory.c`).
  - Lite COW: `dup_mm()` write-protects user PTEs and sets `PTE_COW`; write faults call `resolve_cow()` which either upgrades (refcount==1) or allocates+copies (refcount>1) and updates rmap (`mm/mmap.c`, `mm/memory.c`, `mm/page_alloc.c`, `mm/rmap.c`).
- DIFF (fault layering):
  - Lite folds VMA permission checks + mapping decisions directly inside `do_page_fault()`; there is no Linux-shaped `vm_fault` return taxonomy.
  - Why: keep the initial MM small in a teaching kernel.
  - Impact: harder to extend for file-backed faults and fine-grained fault accounting.
  - Plan: Stage 1 introduces a Linux-shaped `handle_mm_fault()` boundary (even if it still delegates to simplified helpers).
- DIFF (user-visible fault reporting):
  - Lite exits user tasks with a minimal reason code instead of Linux SIGSEGV/SIGBUS semantics.
  - Why: minimal user ABI.
  - Impact: user-mode behavior diverges; tooling expectations differ.
  - Plan: Stage 2 refines internal classification while keeping a small syscall ABI.

### VMA management (mm/mmap.c)
- Object model DIFF: Lite uses a singly linked VMA list without rb-tree ordering, merging/splitting, or `mmap_sem`-style locking semantics.
- Flow/Lifetime DIFF: `dup_mm()` clones VMAs and walks PTEs to establish COW; Linux has a richer `dup_mmap` and anon_vma model for rmap.

Plan (staged, not started yet):
- Stage 0: VMA alignment report (what syscalls exist: mmap/munmap/mprotect/mremap; map to Linux file locations).
- Stage 1: make VMA ordering deterministic (sorted list or rb-tree) and introduce minimal merge/split rules.
- Stage 2: introduce Linux-like anon_vma/rmap model incrementally (or explicitly keep DIFF with clear limitations).

VMA alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + DIFF/Why/Impact/Plan).
- Stage 1: DONE (VMA list insertion is deterministic via `vm_start` ordering; adjacent compatible VMAs are merged to reduce fragmentation).
- Stage 2: PARTIAL (anonymous VMAs now carry a minimal `anon_vma_id` lineage; clone/split paths preserve it and merge requires matching lineage, but full Linux `anon_vma` objects/locking are still a documented DIFF).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux VMA management is ordered (rb-tree in the 2.6 era) and protected by `mmap_sem`; syscalls go through `do_mmap_pgoff()`/`do_munmap()`/`do_mprotect()` and may split/merge VMAs (`linux2.6/mm/mmap.c`).
  - Lite implements `do_mmap()`/`do_munmap()`/`do_mprotect()`/`do_mremap()` over a singly-linked `mm->mmap` list, with local splitting and coarse interrupt masking (`mm/mmap.c`).
- Implemented semantics (Lite):
  - `do_mmap()` finds a gap starting from `brk`/`end_code`, creates an anonymous VMA node, and relies on faults to populate PTEs.
  - `do_munmap()` edits the VMA list (remove/shrink/split), then frees mapped user pages and rmap entries in the range.
  - `dup_mm()` clones the VMA list, then walks the source VMA pages, write-protects and marks `PTE_COW`, maps into the new pgd, and increments page refcounts.
- DIFF (ordering + overlap discipline):
  - Lite VMAs are not stored in a deterministic order; gap search and lookup behavior depends on insertion history.
  - Why: minimal data structure.
  - Impact: fragmentation behavior differs; harder to converge to Linux `find_vma` semantics.
  - Plan: Stage 1 sorts VMAs by `vm_start` and adds basic merge rules for adjacent compatible VMAs.

### rmap / reclaim / swap (mm/rmap.c, vmscan.c, swap.c)
- Semantics DIFF: Lite rmap stores at most one (mm,vaddr) directly in `struct page` and only supports a minimal extra list; Linux uses anon_vma and mapping-based rmap with strict lock ordering.
- Semantics DIFF: reclaim is "pick one page" style; Linux uses LRU lists (active/inactive), page isolation, and scan control heuristics.
- Semantics DIFF: swap is in-memory slots and restricted to current mm; Linux swap is global, device-backed, and supports swapcache/swapin readahead.
- External semantics DIFF: page cache reclaim is a linear scan of `mapping_list`; Linux uses radix tree/xarray tags, LRU, and shrinkers.

Plan (staged, not started yet):
- Stage 0: reclaim/swap alignment report (explicitly state what is supported: anon vs file, swap device vs in-memory, shared mappings).
- Stage 1: introduce minimal LRU lists per zone (even if only inactive list) and page isolation primitives to avoid random scans.
- Stage 2: separate "file cache reclaim" and "anon reclaim" controls (Linux `scan_control::may_writepage/may_swap/may_unmap` shape).
- Stage 3: evolve swap slots to a Linux-like swap entry model (at least swap map + swap entry encoding), or mark as long-term DIFF if swap device is out-of-scope.

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux reclaim is LRU-based with scan control, and swap is global with swap entries/swapcache (`linux2.6/mm/vmscan.c`, `linux2.6/mm/swap*.c`).
  - Lite reclaim is minimal: try to free one clean pagecache page first, else swap out one anon-mapped page; swap is an in-memory slot table (`mm/vmscan.c`, `mm/swap.c`, `mm/filemap.c`).
- Implemented semantics (Lite):
  - `try_to_free_pages()` first calls `page_cache_reclaim_one()` (drops one clean cache page), then scans PFNs in the target zone to find a mapped page and calls `swap_out_page()`.
  - `swap_out_page()` only supports a very narrow case: `mapcount==1`, anon page (`page->map_mm` set), and no secondary rmap list; it unmaps, frees the phys page, and stores a page-sized copy in a `SWAP_SLOTS` array keyed by `(mm,vaddr)`. Page contents are copied via directmap, so the faulting task does not need to own the target mm.
  - `swap_in_mm()` faults a swapped-out page back by allocating a new page, mapping it, and copying data.
- DIFF (LRU and isolation):
  - Lite has no per-zone LRU lists and no page isolation primitives.
  - Why: reduce complexity.
  - Impact: reclaim efficiency and fairness differ; scanning is linear and can miss reclaimable pages.
  - Plan: Stage 1 introduces minimal LRU list(s) and isolation hooks.
- DIFF (swap model scope):
  - Lite swap is process-local, in-memory only, bounded by `SWAP_SLOTS`, and requires `current->mm` ownership.
  - Why: teaching kernel simplification.
  - Impact: behavior differs radically from Linux swap; not suitable for memory overcommit patterns.
  - Plan: Stage 3 either evolves toward a Linux-like swap entry model or marks swap device as out-of-scope with explicit limitations.

reclaim/swap alignment status (implemented):
- Stage 0: DONE (documented supported reclaim+swap scope and the key Linux DIFF boundaries).
- Stage 1: DONE (introduced a minimal per-zone inactive LRU list and one-page isolation; vmscan reclaims anon pages via LRU rather than PFN linear scans).
- Stage 2: DONE (added a minimal `scan_control`-like control plane and separated file-cache reclaim/writeback and anon swap-out based on `may_writepage/may_unmap/may_swap`).

### Slab / kmalloc (mm/slab.c)
- Semantics DIFF: Lite slab is a simple per-page freelist with fixed size classes; Linux slab has cache objects, per-cpu caching, debug options, and NUMA awareness.
- Flow/Lifetime DIFF: no shrink/reclaim hooks; Linux slab interacts with reclaim via shrinkers and cache reaping.

Plan (staged, not started yet):
- Stage 0: slab alignment report (map `kmalloc/kfree/kmem_cache_*` behavior to Linux expectations).
- Stage 1: add minimal cache metadata and optional reclaim hook (even if not full shrinker infrastructure).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux provides slab allocators (slab/slub/slob) with `kmalloc`/`kfree` and `kmem_cache_*` APIs, and can integrate with reclaim via shrinkers (`linux2.6/mm/slab*.c`).
  - Lite provides a fixed-size-class slab allocator for small objects and uses page allocation for larger objects (`mm/slab.c`).
- Implemented semantics (Lite):
  - Size classes: {8..2048} bytes; small allocations come from per-size `kmem_cache` whose slabs are one page each.
  - Large allocations: `kmalloc_large()` allocates 2^order pages and prefixes a small header to allow `kfree()` to `free_pages()`.
  - Free path: small frees locate the slab via a linear `slab_map[]` scan keyed by directmap page base.
- DIFF (lifetime/reclaim integration):
  - No shrinkers and no slab cache reclaim; slabs are not returned to the page allocator.
  - Why: keep allocator simple.
  - Impact: memory can fragment and stay pinned in slabs; behavior differs from Linux under pressure.
  - Plan: Stage 1 adds a minimal reclaim hook (or documents a hard limitation) and avoids hidden leaks.

slab alignment status (implemented):
- Stage 0: DONE (documented API shape + size classes + large allocation header model + major DIFFs).
- Stage 1: DONE (added a minimal slab reclaim hook and wired it into vmscan so empty slab pages can be returned to the page allocator under pressure).

### vmalloc / ioremap (mm/vmalloc.c)
- Semantics DIFF: vmalloc uses a linear bump allocator; vfree only works for tracked blocks and does not return address space for reuse; `iounmap` is a no-op.
- Flow/Lifetime DIFF: Linux vmalloc has vmap/vunmap, address space management, and proper unmap of page tables.

Plan (staged, not started yet):
- Stage 0: vmalloc alignment report (map current vmalloc/ioremap usage and required semantics).
- Stage 1: implement free/unmap semantics for vmalloc/ioremap (no-op `iounmap` should be eliminated or explicitly DIFF with impact).

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux `vmalloc`/`ioremap` allocate virtual address space and create page table mappings for (typically) non-RAM regions such as PCI MMIO (`linux2.6/mm/vmalloc.c`).
  - Linux requires correct handling for non-page-aligned ioremap requests and provides `iounmap()` for teardown.
  - Lite provides a linear vmalloc range allocator and implements `ioremap()` by mapping pages into the kernel pgd; `iounmap()` is currently a no-op (`mm/vmalloc.c`).
- DIFF (address space management):
  - Lite does not reuse freed vmalloc address space and only provides limited vfree tracking.
  - Why: keep allocator small and deterministic.
  - Impact: long-running workloads can exhaust vmalloc space; fragmentation rules differ from Linux.
  - Plan: Stage 1 adds unmap/free semantics or documents the long-term DIFF boundary clearly.
- DIFF (iounmap semantics):
  - Lite `iounmap()` is a no-op.
  - Why: no vmap/vunmap infrastructure.
  - Impact: MMIO mappings can leak; cannot safely tear down devices that rely on `iounmap`.
  - Plan: Stage 1 either implements unmap for ioremap or marks it as an explicit supported limitation and audits driver expectations.
- Implemented note (recent fix relevance):
  - Lite `ioremap()` now handles non-page-aligned physical addresses by aligning the physical base down, mapping full span, and returning virtual + page offset. This is required for modern PCI BAR capability mappings.

vmalloc/ioremap alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + current Lite semantics and the key DIFFs + plan).
- Stage 1: DONE (vmalloc/ioremap allocations are tracked and reusable; `vfree()` and `iounmap()` now unmap kernel page tables and release vmalloc address space for reuse).

### Page cache / writeback (mm/filemap.c)
- Semantics DIFF: page cache is per-mapping linked list; Linux uses radix tree/xarray and integrates with writeback throttling (`balance_dirty_pages`).
- Flow/Lifetime DIFF: writeback is a synchronous sweep; Linux has background writeback and dirty throttling, with clear bdi semantics.

Plan (staged, not started yet):
- Stage 0: pagecache/writeback alignment report (map `address_space` and aops usage; identify required invariants).
- Stage 1: introduce basic indexing/tagging (avoid O(n) scans) and separate clean/dirty lists.
- Stage 2: introduce minimal dirty throttling semantics (even if simplified) to prevent unbounded dirtying under memory pressure.

Stage 0: Alignment Report (current reality)
- Linux concept mapping:
  - Linux page cache is indexed per mapping (radix tree/xarray in modern kernels; radix tree in 2.6), and writeback has background flushing and dirty throttling (`linux2.6/mm/filemap.c`, `linux2.6/mm/page-writeback.c`, `linux2.6/mm/truncate.c`).
  - Lite uses a per-mapping singly linked list of `page_cache_entry` nodes and provides minimal writeback counters and a synchronous flush (`mm/filemap.c`).
- Implemented semantics (Lite):
  - Cache storage: `address_space.pages` is a linear list; lookup is O(n) by `index`.
  - Read: `generic_file_read()` loads pages via `a_ops->readpage` when present, else returns zeros.
  - Write: `generic_file_write()` dirties cache pages and increments `wb_dirty_pages`; for mappings without `writepage`, it extends `i_size`.
  - Truncate: `truncate_inode_pages(mapping, 0)` frees all cached pages and decrements dirty counters; partial truncate is not implemented.
  - Reclaim: `page_cache_reclaim_one()` drops one clean page by walking `mapping_list`.
  - Flush: `writeback_flush_all()` writes dirty pages via `a_ops->writepage` if available.
- DIFF (indexing and scalability):
  - Lite is O(n) for lookup, reclaim, and flush due to linked lists.
  - Why: minimal implementation.
  - Impact: performance diverges from Linux under pagecache pressure; reclaim can be ineffective.
  - Plan: Stage 1 adds basic indexing/tagging and clean/dirty separation.
- DIFF (writeback model):
  - Lite writeback is synchronous and has no dirty throttling or background flusher.
  - Why: keep behavior deterministic.
  - Impact: different latency and memory pressure behavior than Linux; can over-dirty without throttle.
  - Plan: Stage 2 introduces minimal dirty throttling semantics and a background flush hook if needed.

pagecache/writeback alignment status (implemented):
- Stage 0: DONE (documented mapping/aops model + current invariants + major DIFFs).
- Stage 1: DONE (added a simple page-index hash and split clean/dirty page lists so lookup/reclaim/writeback avoid full per-mapping scans).
- Stage 2: DONE (added minimal dirty throttling semantics: writers trigger a bounded synchronous flush when global dirty count crosses a small limit).

## Kernel Core (kernel/)

Linux mapping (vendored):
- Scheduler/core tasking: `linux2.6/kernel/sched.c`, `linux2.6/kernel/sched_*`
- Fork/exit/wait: `linux2.6/kernel/fork.c`, `linux2.6/kernel/exit.c`
- Signals: `linux2.6/kernel/signal.c`
- PID: `linux2.6/kernel/pid.c` (and pid namespaces in later Linux)
- printk/panic: `linux2.6/kernel/printk.c`, `linux2.6/kernel/panic.c`
- Kernel params/init: `linux2.6/kernel/params.c`, `linux2.6/kernel/ksysfs.c`

Lite status (kernel/):
- `kernel/sched.c`: single global runqueue (`task_list_head`) and a simple RR time-slice (`need_resched` global).
- `kernel/fork.c`: minimal `sys_fork()` that clones `mm` and file table; no clone flags or thread model.
- `kernel/exit.c`, `kernel/wait.c`: minimal zombie + `waitpid()` based on a global `exit_waitq`.
- `kernel/signal.c`: minimal `sys_kill()` with a small signal set; signals are handled mostly as "force exit" and parent wakeup.
- `kernel/syscall.c`: int 0x80-like handler using a switch statement (no syscall table); does resched at the end.
- `kernel/printk.c`: console-backed printk with a small ring buffer; no loglevel or locking.
- `kernel/ksysfs.c`: minimal `/sys/kernel` attributes (version/uptime/uevent).
- `kernel/params.c`: minimal `init=` parsing only.

Key gaps (need convergence):

### Scheduler/tasking (kernel/sched.c)
- Object model DIFF: Linux has `task_struct` state model + runqueue (`struct rq`) per CPU, priority, and structured wakeups; Lite has one list and ad-hoc wakeup rules.
- Flow/Lifetime DIFF: Linux separates `need_resched` semantics, preemption model, and interrupt/tick interactions; Lite uses a global `need_resched` and forces resched via `int $0x20`.
- Semantics DIFF: sleep/wakeup are time-based only; no Linux-like waitqueue integration as a first-class wakeup mechanism.

Plan (staged, not started yet):
- Stage 0: scheduler alignment report (map Lite states/tick/yield to Linux `schedule()`/`try_to_wake_up()`/`wake_up` concepts).
- Stage 1: make wakeup a structured primitive (waitqueue-driven) rather than scattered ad-hoc state flips.
- Stage 2: introduce minimal rq abstraction boundary (even if single CPU) so future SMP/preempt work does not rewrite public APIs.

### Fork/exit/wait/signal
- Semantics DIFF: Linux `copy_process` supports clone flags, thread groups, and structured `do_wait`; Lite has a minimal fork/exit model with global scans of `task_list_head`.
- Signals DIFF: Linux has pending signals, signal masks, default/ignored handlers, and delivery rules; Lite currently uses kill as "terminate" plus a parent wakeup.
- Lifetime DIFF: Linux references tasks via refcounting and uses clear reparenting semantics with `init` as reaper; Lite reparenting is a linear scan and task freeing is immediate after wait.

Plan (staged, not started yet):
- Stage 0: process model alignment report (which parts are intentionally out-of-scope: threads, sessions, groups, ptrace).
- Stage 1: introduce Linux-like wait/signal object model boundaries (pending signals + `do_wait` shape), even if only a subset of behaviors is implemented.
- Stage 2: tighten lifetime rules (task references, reparenting invariants, and locking discipline).

### Syscalls/entry/irq boundaries
- Semantics DIFF: Linux uses an arch entry path and a syscall table; Lite uses a large switch in `syscall_handler`.
- Flow/Lifetime DIFF: Linux has clear `from_user` entry/exit accounting, tracing hooks, and restartable syscalls; Lite is minimal.

Plan (staged, not started yet):
- Stage 0: syscall/entry alignment report (map Lite int 128 path to Linux entry path concepts).
- Stage 1: replace switch with a syscall table and stable ABI boundaries (keep the syscall numbers, align the dispatch model).

Syscalls alignment status (implemented):
- Stage 1: PARTIAL (syscall dispatch now uses a Linux-like `sys_call_table[]` while keeping existing syscall numbers).

### printk/panic/ksysfs
- Semantics DIFF: Linux printk is buffered, level-based, and synchronized; Lite prints directly to console and keeps a small ring.
- External semantics DIFF: `/sys/kernel/uevent` in Lite exposes the device uevent buffer; Linux sysfs uevent semantics differ and are per-kobject/per-device rather than a global log.

Plan (staged, not started yet):
- Stage 0: logging alignment report (define what part of Linux printk/kmsg is in-scope).
- Stage 1: tighten printk buffering/locking semantics and stop overloading sysfs nodes with non-Linux meanings.

kernel core alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + Lite current status + major DIFFs and staged plan).
- Stage 1: PARTIAL (printk return semantics now match Linux-like "printed byte count"; broader scheduler/process/syscall/signal convergence remains staged).

## Kernel Libraries (lib/)

Linux mapping (vendored):
- Strings/memory: `linux2.6/lib/string.c`, `linux2.6/lib/vsprintf.c`, plus arch-specific `linux2.6/arch/x86/lib/*`
- Data structures: `linux2.6/lib/rbtree.c`, `linux2.6/lib/bitmap.c`, `linux2.6/lib/idr.c`, `linux2.6/lib/radix-tree.c`
- Kobject helpers: `linux2.6/lib/kobject.c`, `linux2.6/lib/kobject_uevent.c`, `linux2.6/lib/kref.c`
- Parser: `linux2.6/lib/parser.c`

Lite status (lib/):
- `lib/libc.c`: minimal `memset/memcpy/strlen/strcpy/strcmp/strncmp/strcat/itoa/printf/strdup`
- `lib/bitmap.c`: a subset of `__bitmap_*` primitives
- `lib/rbtree.c`: core rb insert/erase helpers (Linux-like algorithm)
- `lib/idr.c`: simplified ID allocator backed by a growable pointer array
- `lib/kref.c`: minimal refcount helpers (non-atomic)
- `lib/kobject.c`: minimal `kobject/kset/subsystem` glue with sysfs directory creation
- `lib/parser.c`: minimal match_token/match_int helpers (Linux-like API)

Key gaps (need convergence):
- Semantics DIFF (string/memory): `memcpy` does not guarantee overlap safety; Linux has distinct `memmove` semantics.
  - Ensure API names match semantics (do not let callers rely on undefined overlap behavior).
- Semantics DIFF (printf): `vsnprintf` is now present with Linux-like return convention, but formatting coverage remains simplified; printk formatting is still simplified.
- Concurrency DIFF (kref): Linux kref is built on atomic/refcounting semantics; Lite uses a plain counter.
  - This is acceptable for single CPU teaching usage but must be marked DIFF and should not be exposed as a "Linux-strength" lifetime guarantee.
- Object model DIFF (kobject): Linux kobject has strict lifetime rules, uevent semantics, and kset/klist relationships; Lite uses a simplified child linkage and minimal sysfs hooks.
- Data structure DIFF (idr): Linux idr uses radix-tree-like layering and supports allocation ranges and reuse semantics; Lite uses a growable array and linear scan.

lib alignment status (implemented):
- Stage 0: NOT STARTED (full lib alignment report still pending).
- Stage 1: PARTIAL (added `memmove` and minimal `vsnprintf` with Linux-like return semantics; printk now returns printed byte count).

Plan (staged):
- Stage 0: lib alignment report (for each exported function: Linux semantics, Lite semantics, and DIFF notes).
- Stage 2: tighten lifetime primitives shape (move `kref` toward Linux refcounting semantics; clarify locking requirements).
- Stage 3: evolve idr toward Linux-like allocation semantics (range allocation, reuse rules), or explicitly mark long-term DIFF.

## Architecture (arch/x86)

Linux mapping (vendored):
- Boot/entry: `linux2.6/arch/x86/boot/*`, `linux2.6/arch/x86/kernel/entry.S` (entry path shape)
- Traps/IRQs: `linux2.6/arch/x86/kernel/irq.c`, `linux2.6/arch/x86/kernel/traps.c`, `linux2.6/arch/x86/kernel/i8259.c`
- Descriptor tables: `linux2.6/arch/x86/kernel/desc.c`, `arch/x86/include/asm/desc.h`
- Setup: `linux2.6/arch/x86/kernel/setup.c` (and early platform init flow)

Lite status (arch/x86):
- Boot: `arch/x86/boot/boot.s` (multiboot entry and early handoff)
- Descriptor tables: `arch/x86/kernel/gdt.c`, `arch/x86/kernel/idt.c`
- IRQ/trap glue: `arch/x86/kernel/interrupt.s`, `arch/x86/kernel/isr.c`, `arch/x86/kernel/irq.c`
- Early setup: `arch/x86/kernel/setup.c` registers a few platform devices and calls `init_gdt/init_idt`

Key gaps (need convergence):
- Entry path shape DIFF: Lite uses a single `isr_common_stub/irq_common_stub` and dispatches in C; Linux uses a structured entry path with clear separation of exceptions/IRQs/syscalls and well-defined pt_regs saving/restoring rules.
- IRQ model DIFF: Lite remaps/unmasks PIC inside `isr.c` and handles EOIs directly in `irq_handler`; Linux has an irq subsystem (`irq_desc` + `irq_chip`) and i8259 is a chip implementation.
- Syscall dispatch DIFF: Lite uses an in-handler switch; Linux uses a syscall table and arch entry glue.
- Capability/CPU feature DIFF: no APIC/IOAPIC, no SMP, no TSC calibration, no TLS/ldt model, no usercopy hardening; acceptable as out-of-scope but must not leak into public interfaces.

Plan (staged, not started yet):
- Stage 0: arch/x86 alignment report (map Lite entry/irq/syscall paths to Linux x86 equivalents).
- Stage 1: stabilize pt_regs + entry ABI shape (clear rules for what is saved/restored and when interrupts are enabled/disabled).
- Stage 2: introduce a minimal Linux-like irq core boundary (irq_desc/irq_chip) and move i8259 PIC logic into an irqchip layer.
- Stage 3: switch syscall dispatch to a syscall table (keep current syscall numbers; align dispatch model to Linux patterns).
- Stage 4: clarify and isolate out-of-scope features (APIC/SMP/TLS/ldt) so later convergence does not rewrite unrelated code.

arch/x86 alignment status (implemented):
- Stage 0: DONE (documented mapping + current Lite entry/irq/syscall shape + DIFFs).

## Driver Core (drivers/base)

Linux mapping (vendored):
- `linux2.6/drivers/base/{core,bus,driver,class,uevent}.c`

Lite status:
- `drivers/base/{core,bus,driver,class,uevent}.c`: minimal driver core exists and uses Linux terms

Key gaps (need convergence):
- Matching semantics DIFF: modalias-based binding and standardized uevent fields are incomplete.
- Lifetime DIFF: missing Linux-like reference counting and detachment ordering constraints.

Plan (staged, not started yet):
- Stage 0: driver core alignment report (what is implemented vs DIFF).
- Stage 1: modalias + bus.match semantics and standard uevent fields (no custom event names).
- Stage 2: minimal reference counting/locking constraints for safe bind/unbind.

driver core alignment status (implemented):
- Stage 0: DONE (documented current object model, sysfs/uevent surface, and lifetime/matching DIFFs).
- Stage 1: PARTIAL (standard uevent actions/fields are in place; `modalias` is now a generic driver-core device attribute; uevents carry Linux-like `DEVNAME/DEVMODE/DEVUID/DEVGID/SEQNUM`; and the existing `drivers_probe` path accepts either a device name or modalias string to trigger reprobe. Full Linux-style modalias-to-module autoload remains pending).
- Stage 2: PARTIAL (`driver_probe_device()` no longer steals an already bound device; explicit reprobe must detach first via `device_release_driver()`/`device_reprobe()`; device removal now also detaches the device from the deferred-probe queue; `driver_unregister()` drops the driver kobject reference via `kobject_put()`; devices now hold a driver kobject ref while bound (released on unbind); and `kset_add()/kset_remove()` plus `subsystem_register()/subsystem_unregister()` now hold/release parent kobject references symmetrically).

## Timekeeping / Clocksource

Linux mapping (vendored):
- `linux2.6/kernel/time/{clocksource.c,clockevents.c,tick-*.c,timekeeping.c,jiffies.c}`
- `linux2.6/drivers/clocksource/i8253.c`: x86 PIT usage

Lite status:
- `drivers/clocksource/timer.c`: PIT tick + basic time reads

Key gaps (need convergence):
- Layering DIFF: missing clocksource vs clockevent separation; timekeeping is tick-centric and driver-visible.

timekeeping alignment status (implemented):
- Stage 0: DONE (documented Linux mapping + current Lite tick-centric timekeeping and layering DIFFs).
- Stage 1: PARTIAL (introduced a minimal Linux-like clockevents/tick boundary: PIT provides a `clock_event_device`, and the generic tick layer calls into `time_tick()`. Tick rate is fixed at `HZ`, matching Linux jiffies semantics; dynamic HZ changes remain out-of-scope).

Plan (staged):
- Stage 2: move time conversion/udelay/timeout semantics into Linux-like layers; avoid drivers reading PIT directly.

## Input / TTY / Console

Linux mapping (vendored):
- Input: `linux2.6/drivers/input/input.c`, `drivers/input/serio/i8042.c`, `drivers/input/keyboard/atkbd.c`, `drivers/input/evdev.c`
- TTY: `linux2.6/drivers/tty/tty_io.c`, `tty_ldisc.c`, `n_tty.c`, `drivers/tty/serial/serial_core.c`
- Console/VT: `linux2.6/drivers/tty/vt/*` and `drivers/video/console/*`

Lite status:
- `drivers/input/keyboard.c`: reads i8042 and injects ASCII toward TTY
- `drivers/tty/tty.c`: minimal tty buffer + read/write
- `drivers/tty/serial/serial.c`: minimal serial driver
- `drivers/video/console/*`: minimal VGA text console output

Key gaps (need convergence):
- Input semantics DIFF: keyboard is tightly coupled to TTY; lacks Linux input core/serio/atkbd shape.
- TTY semantics DIFF: lacks ldisc (`n_tty`) and Linux-like tty driver object model (`tty_driver`, lifetime).
- Console boundary DIFF: console should primarily be output path; input should be routed via input/tty layers.

Plan (staged, not started yet):
- Stage 0: report coupling points (where input/tty/console are cross-calling).
- Stage 1: minimal Linux shapes: `serio` + `i8042` + `atkbd` + event dispatch, and `tty_driver` registration basics.
- Stage 2: introduce minimal `n_tty` ldisc for canonical input handling; move policy out of keyboard/console drivers.

input/tty/console alignment status (implemented):
- Stage 0: DONE (documented mapping + current Lite coupling points + major DIFFs and staged plan).

## Notes

- This file is the canonical place for subsystem alignment plans. `Documentation/QA.md` should only keep short operational Q&A and link here for alignment roadmaps.
