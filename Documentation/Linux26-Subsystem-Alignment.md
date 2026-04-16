Linux 2.6 Subsystem Alignment (Remaining Work)

This document tracks only the subsystems that are NOT fully aligned yet.
Subsystems that are already marked as DONE have been removed from this file to keep focus on remaining work.

Principles:
- No new concepts: every term/structure/interface must map to a Linux 2.6 concept.
- Naming must follow Linux vocabulary (e.g., kobject/kset, clockevents, sys_call_table, etc.).
- If a DIFF is unavoidable, document DIFF with Why/Impact/Plan and do not hide it behind ad-hoc behavior.
- Prefer staged convergence: Stage 0 report -> minimal Linux-shaped boundary -> expand semantics.


Progress Snapshot

Kernel / arch / driver core:
- kernel core (sched/fork/exit/wait/signal): `STAGE0 ONLY` (needs Stage 1+)
- kernel core (syscalls): `IN PROGRESS` (Stage 0 DONE; Stage 1 partial)
- kernel core (printk): `IN PROGRESS` (Stage 0 DONE; Stage 1 partial)
- lib/: `IN PROGRESS` (Stage 1 partial; Stage 0 report pending)
- arch/x86: `STAGE0 ONLY` (needs Stage 1+)
- drivers/base: `IN PROGRESS` (Stage 0 DONE; Stage 1 partial; Stage 2 partial)
- timekeeping/clocksource: `IN PROGRESS` (Stage 0 DONE; Stage 1 partial)
- input/tty/console: `STAGE0 ONLY` (needs Stage 1+)

Filesystems:
- ramfs: `STAGE0 ONLY` (needs Stage 1)
- minixfs: `STAGE0 ONLY` (needs Stages 1-2)
- sysfs: `IN PROGRESS` (Stage 0 DONE; Stage 1 DONE; Stage 2 partial)

Memory management (mm/):
- VMA management: `IN PROGRESS` (Stage 0 DONE; Stage 1 DONE; Stage 2 partial)
- reclaim/swap: `IN PROGRESS` (Stages 0-2 DONE; Stage 3 pending decision)


Acceptance Queue

Use this queue as the execution order. Each step is considered complete only when its acceptance criteria are all satisfied.

Step 4. `drivers/base` Stage 1 complete
- Scope:
  - `drivers/base/core.c`, `drivers/base/driver.c`, `drivers/base/uevent.c`.
- Goal:
  - Finish Linux-like device/driver public surface (attributes + uevent environment).
- Acceptance:
  - Uevent field set is stable and documented.
  - `modalias` attribute and `DEVNAME/MAJOR/MINOR/SEQNUM` behavior are covered by smoke or focused checks.
  - Any missing module autoload behavior is explicitly marked as out-of-scope DIFF.
  - `make -j4 && make smoke-512` passes.

Step 5. `drivers/base` Stage 2 complete
- Scope:
  - bind/unbind/reprobe, deferred probe, driver/device kobject lifetime.
- Goal:
  - Close lifetime and reprobe corner cases so Lite behavior matches Linux driver-core boundaries.
- Acceptance:
  - Reprobe path requires explicit unbind before rebinding.
  - Driver/device unregister paths drop all refs symmetrically.
  - Deferred-probe queue cannot retain removed devices.
  - Smoke covers bind/unbind/reprobe on at least one bus.

Step 6. `timekeeping/clocksource` Stage 2
- Scope:
  - `kernel/time.c`, `kernel/clockevents.c`, timer/PIT users.
- Goal:
  - Stop direct PIT-driven time assumptions from leaking into generic code.
- Acceptance:
  - Generic delay/timeout/time conversion helpers no longer require drivers to read PIT state directly.
  - Tick rate remains fixed at `HZ` and that rule is documented.
  - `make -j4 && make smoke-512` passes.

Step 7. `arch/x86` Stage 1
- Scope:
  - entry/irq/register-save ABI, `pt_regs` contract.
- Goal:
  - Make x86 entry ABI Linux-shaped enough that syscall/irq/fault code rely on one consistent saved-register contract.
- Acceptance:
  - `pt_regs` save/restore contract is documented in code or doc.
  - Syscall, IRQ, and fault handlers consume the same stable register layout.
  - No ad-hoc per-handler register assumptions remain.

Step 8. `kernel/sched + wait/signal` Stage 1
- Scope:
  - `kernel/sched.c`, `kernel/wait.c`, `kernel/signal.c`, related task wakeup paths.
- Goal:
  - Replace scattered wakeups with Linux-shaped wait/wakeup boundaries.
- Acceptance:
  - At least one waitqueue-like primitive is the canonical wakeup path.
  - `waitpid` and signal wakeups stop depending on open-coded task-list scans where avoidable.
  - Document remaining unsupported signal semantics explicitly.
  - `make -j4 && make smoke-512` passes.

Step 9. `input/tty/console` Stage 1
- Scope:
  - keyboard/input path, tty registration, console input flow.
- Goal:
  - Introduce minimal Linux shapes: serio/i8042/atkbd split and tty_driver registration basics.
- Acceptance:
  - Keyboard hardware logic is no longer the policy owner for line discipline behavior.
  - A tty-driver-like registration boundary exists.
  - Console input still works in smoke/manual boot verification.

Step 10. `ramfs` Stage 1
- Scope:
  - `fs/ramfs/ramfs.c`
- Goal:
  - Align inode creation/lifetime shape to Linux ramfs idioms.
- Acceptance:
  - Ramfs inode creation follows the same conceptual boundary for regular files/dirs/symlinks.
  - Rootfs boot and file I/O smoke continue to pass.

Step 11. `minixfs` Stage 1
- Scope:
  - `fs/minixfs/minixfs.c`
- Goal:
  - Replace mount-time directory prepopulation with Linux-like on-demand lookup.
- Acceptance:
  - Mount no longer eagerly scans and instantiates full directory children.
  - Lookup/readdir still works for existing minixfs smoke/use cases.

Step 12. `minixfs` Stage 2
- Scope:
  - super/inode lifecycle hooks.
- Goal:
  - Add Linux-shaped lifecycle boundaries even if writeback stays simplified.
- Acceptance:
  - `put_super`/`evict_inode`-style ownership boundaries exist or exact Lite equivalent is documented.
  - Unmount/remount does not rely on leaked inode state.

Step 13. `sysfs` Stage 2 complete
- Scope:
  - `fs/sysfs/sysfs.c`, VFS symlink semantics.
- Goal:
  - Finish Linux-like symlink behavior without breaking reachability.
- Acceptance:
  - Relative sysfs links work where Linux expects them.
  - `readlink`/symlink traversal semantics match VFS plan.
  - `/sys/class`, `/sys/bus/*/devices`, and bind/unbind smoke remain green.

Step 14. `mm/VMA` Stage 2 complete
- Scope:
  - `mm/mmap.c`, `mm/rmap.c`
- Goal:
  - Decide and implement the minimal anon_vma lineage boundary.
- Acceptance:
  - Merge/split/clone preserve anon lineage consistently.
  - Remaining lack of full Linux anon_vma objects/locking is either removed or explicitly frozen as DIFF.
  - `make -j4 && make smoke-512` passes.

Step 15. `mm/reclaim/swap` Stage 3 decision
- Scope:
  - `mm/vmscan.c`, `mm/swap.c`, `mm/rmap.c`
- Goal:
  - Either converge further toward Linux swap entry model or explicitly stop and document the limitation.
- Acceptance:
  - One of the following is true:
  - A minimal Linux-like swap entry encoding + swap map exists.
  - Or the doc explicitly marks swap device/global swap as out-of-scope with supported/unsupported cases listed.
  - The chosen direction is reflected in smoke or targeted regression checks.


Kernel Core (kernel/)

Linux mapping (vendored):
- Scheduler: `linux2.6/kernel/sched.c`
- Fork/exit/wait: `linux2.6/kernel/fork.c`, `linux2.6/kernel/exit.c`
- Signals: `linux2.6/kernel/signal.c`
- Syscalls/entry: `linux2.6/arch/x86/kernel/entry_32.S` + `linux2.6/arch/x86/kernel/syscall_table_32.S` (concept mapping), `linux2.6/kernel/sys.c` (syscall bodies)
- printk/panic: `linux2.6/kernel/printk.c`, `linux2.6/kernel/panic.c`

Status:
- sched/fork/exit/wait/signal: Stage 0 only (documented mapping + DIFFs; implementation convergence pending)
- syscalls: Stage 1 DONE (Linux-like `sys_call_table[]` dispatch; `NR_syscalls` is the only bounds source; smoke passes)
- printk: Stage 1 DONE (return semantics selftested; `/sys/kernel/uevent_helper` + `/sys/kernel/uevent_seqnum` are Linux-like; smoke passes)

Next steps (plan):
- Scheduler/tasking:
  - Stage 1: make wakeup a structured primitive (waitqueue-driven), reduce ad-hoc state flips.
  - Stage 2: introduce a minimal rq abstraction boundary (even if single CPU).
- Fork/exit/wait/signal:
  - Stage 1: introduce Linux-like `do_wait`/pending-signal boundaries (subset OK, shape must be Linux-like).
  - Stage 2: tighten lifetime rules (task refs, reparenting invariants, locking discipline).
- Syscalls/entry:
  - Stage 1: finish syscall table shape: audit full SYS_* coverage, unify bounds via `NR_syscalls`.
  - Stage 2: clarify entry/exit invariants (from_user accounting, restartability out-of-scope explicitly).
- printk:
  - Stage 1: define what is in-scope (kmsg/loglevel/locking); stop overloading sysfs nodes with non-Linux meaning.


Kernel Libraries (lib/)

Linux mapping (vendored):
- `linux2.6/lib/string.c`, `linux2.6/lib/vsprintf.c`
- `linux2.6/lib/kref.c`, `linux2.6/lib/kobject.c`
- `linux2.6/lib/idr.c`

Status:
- Stage 0: DONE (exported API audit completed for libc/vsprintf/kref/kobject/idr; each symbol is tagged OK or DIFF)
- Stage 1: PARTIAL (memmove semantics, minimal vsnprintf return semantics, printk return semantics)

Stage 0 audit (exported APIs)
- Covered files:
  - `include/linux/libc.h`
  - `include/linux/vsprintf.h`
  - `lib/kref.c`
  - `lib/kobject.c`
  - `lib/idr.c`
- `include/linux/libc.h`
  - `memset/memcpy/memmove/strcpy/strlen/strcmp/strncmp/strcat`: `OK`
    - Linux mapping: `linux2.6/lib/string.c`
    - Notes: core semantics align; `memmove` now provides overlap-safe behavior.
  - `strdup`: `DIFF`
    - Linux mapping: kernel typically uses `kstrdup()` rather than exposing libc-style `strdup`.
    - Why: Lite keeps a small libc-style helper for convenience.
    - Impact: naming/API surface is less Linux-kernel-like.
    - Plan: either rename call sites toward `kstrdup`-style helper or explicitly keep as Lite-only helper outside kernel-facing alignment claims.
  - `itoa`: `DIFF`
    - Linux mapping: Linux uses formatting helpers (`snprintf`/`scnprintf`) rather than a generic exported `itoa`.
    - Why: small helper kept for simple users.
    - Impact: non-Linux helper remains in exported surface.
    - Plan: reduce internal callers and prefer `snprintf`-style formatting.
  - `printf`: `DIFF`
    - Linux mapping: kernel-facing interface is `printk`, not libc `printf`.
    - Why: Lite keeps a convenience wrapper.
    - Impact: kernel/user helper naming diverges from Linux.
    - Plan: keep explicit as Lite convenience; do not treat as Linux-kernel API.
  - `inb/outb/inw/outw/inl/outl`: `OK`
    - Linux mapping: x86 port I/O helpers/macros (`asm/io.h` conceptually).
- `include/linux/vsprintf.h`
  - `vsnprintf/snprintf`: `OK`
    - Linux mapping: `linux2.6/lib/vsprintf.c`
    - Notes: return semantics align; format coverage is still a documented subset.
- `lib/kref.c`
  - `kref_init/kref_get/kref_put`: `PARTIAL DIFF`
    - Linux mapping: `linux2.6/lib/kref.c`
    - Why: naming and release-callback shape align, but implementation is non-atomic and lacks Linux memory-order/concurrency guarantees.
    - Impact: acceptable in single-core teaching kernel; not Linux-safe under SMP/preempt concurrency.
    - Plan: Stage 2 clarifies/refines refcounting semantics and locking assumptions.
- `lib/kobject.c`
  - `kobject_init/kobject_init_with_ktype/kobject_add/kobject_get/kobject_put`: `PARTIAL DIFF`
    - Linux mapping: `linux2.6/lib/kobject.c`
    - Why: naming/object model align, but lifetime/list/sysfs coupling is simplified.
    - Impact: behavior is Linux-shaped, not Linux-complete.
    - Plan: keep tightening lifetime rules through driver-core/sysfs work.
  - `kset_init/kset_add/kset_remove/subsystem_register/subsystem_unregister`: `PARTIAL DIFF`
    - Linux mapping: Linux kset/subsystem registration path.
    - Why: conceptual boundaries align, but locking/refcount discipline is simplified.
    - Impact: ordering/corner cases may still diverge from Linux.
    - Plan: continue Stage 2 lifetime tightening with driver-core work.
- `lib/idr.c`
  - `idr_init/idr_pre_get/idr_get_new_above/idr_get_new/idr_find/idr_remove`: `PARTIAL DIFF`
    - Linux mapping: `linux2.6/lib/idr.c`
    - Why: API naming aligns, but implementation is a flat growable slot array, not Linux radix-layered idr.
    - Impact: allocation/reuse/scaling semantics differ.
    - Plan: Stage 3 decides whether to converge further or freeze as documented long-term DIFF.

Next steps (plan):
- Stage 2: tighten lifetime primitives (move kref toward Linux refcounting semantics; clarify locking requirements).
- Stage 3: evolve idr toward Linux-like allocation semantics (range allocation, reuse rules), or mark long-term DIFF.


Architecture (arch/x86)

Linux mapping (vendored):
- Entry/IRQ: `linux2.6/arch/x86/kernel/entry_32.S`, `linux2.6/arch/x86/kernel/irq.c`
- Syscall path: Linux int80/sysenter entry path + syscall table dispatch (concept mapping)

Status:
- Stage 0 only (mapping + DIFFs documented); implementation stages pending.

Next steps (plan):
- Stage 1: stabilize pt_regs + entry ABI shape (clear rules for what is saved/restored and when interrupts are enabled/disabled).
- Stage 2: introduce a minimal Linux-like irq core boundary (irq_desc/irq_chip) and move i8259 PIC logic into an irqchip layer.
- Stage 3: switch syscall dispatch to a syscall table (already partially done in kernel core; arch entry invariants still need convergence).
- Stage 4: clarify and isolate out-of-scope features (APIC/SMP/TLS/ldt) so convergence does not rewrite unrelated code.


Driver Core (drivers/base)

Linux mapping (vendored):
- Core: `linux2.6/drivers/base/core.c`, `linux2.6/drivers/base/driver.c`
- Uevent: `linux2.6/lib/kobject_uevent.c` (concept mapping)

Status:
- Stage 0 DONE, Stage 1 partial, Stage 2 partial.

Next steps (plan):
- Stage 1:
  - Finish uevent field coverage + ordering discipline (Linux-like `ACTION/DEVPATH/SUBSYSTEM/MODALIAS/DEVNAME/MAJOR/MINOR/SEQNUM`).
  - Keep explicit DIFF for module autoload (out-of-scope unless added).
- Stage 2:
  - Tighten bind/unbind/reprobe semantics and lifetime rules (kobject refs, deferred-probe correctness).


Timekeeping / Clocksource / Clockevents

Linux mapping (vendored):
- `linux2.6/kernel/time/clockevents.c` (concept mapping: clock_event_device registration)
- `linux2.6/kernel/time/tick-common.c` (concept mapping: tick handling)

Status:
- Stage 0 DONE; Stage 1 partial (minimal clockevents/tick boundary; periodic only).

DIFF notes (must remain explicit):
- Tick rate is fixed at `HZ` (Linux-like jiffies semantics); dynamic HZ changes are out-of-scope.
- Oneshoot/highres/NOHZ are out-of-scope until clocksource/timekeeping layering exists.

Next steps (plan):
- Stage 2: move time conversion/udelay/timeout semantics into Linux-like layers; avoid drivers reading PIT directly.


Input / TTY / Console

Linux mapping (vendored):
- i8042/atkbd: `linux2.6/drivers/input/serio/i8042.c`, `linux2.6/drivers/input/keyboard/atkbd.c`
- tty core: `linux2.6/drivers/tty/tty_io.c`, `linux2.6/drivers/tty/n_tty.c`

Status:
- Stage 0 only (mapping + coupling points documented); implementation pending.

Next steps (plan):
- Stage 1: minimal Linux shapes: serio + i8042 + atkbd + event dispatch, and tty_driver registration basics.
- Stage 2: minimal n_tty line discipline for canonical input handling; move policy out of keyboard/console drivers.


Filesystems (ramfs / minixfs / sysfs)

ramfs

Linux mapping (vendored):
- `linux2.6/fs/ramfs/inode.c`

Status:
- Stage 0 DONE; Stage 1 pending.

Next steps (plan):
- Stage 1: align ramfs inode creation/lifetime to Linux idioms (even if caching remains simplified).


minixfs

Linux mapping (vendored):
- `linux2.6/fs/minix/*`

Status:
- Stage 0 DONE; Stage 1-2 pending.

Next steps (plan):
- Stage 1: move toward Linux lookup model (populate dentries on demand, not via mount-time directory scan).
- Stage 2: add minimal super/inode lifecycle hooks (put_super/evict_inode naming/shape; writeback can remain simplified but must be explicit).


sysfs

Linux mapping (vendored):
- `linux2.6/fs/sysfs/*` (concept mapping: kobject-bound directory tree, attributes, symlinks)

Status:
- Stage 0 DONE; Stage 1 DONE; Stage 2 partial.

Remaining gaps:
- Symlink semantics remain simplified: absolute target storage and read-style exposure; relative targets remain deferred.

Next steps (plan):
- Stage 2: align symlink semantics to Linux + VFS symlink plan (relative targets + readlink semantics), without breaking reachability.


Memory Management (mm/)

VMA management (mm/mmap.c)

Linux mapping (vendored):
- `linux2.6/mm/mmap.c`

Status:
- Stage 0 DONE; Stage 1 DONE; Stage 2 partial.

Remaining gaps:
- Full Linux anon_vma objects/locking are not present (must remain explicit DIFF).

Next steps (plan):
- Stage 2: continue incremental convergence to Linux anon_vma/rmap model, or explicitly mark long-term DIFF with hard limitations.


reclaim/swap (mm/rmap.c, mm/vmscan.c, mm/swap.c)

Linux mapping (vendored):
- `linux2.6/mm/vmscan.c`, `linux2.6/mm/swap*.c`, `linux2.6/mm/rmap.c`

Status:
- Stages 0-2 DONE; Stage 3 pending decision.

Next steps (plan):
- Stage 3: evolve swap slots toward a Linux-like swap entry model (swap entry encoding + swap map), or explicitly mark swap device as out-of-scope with clear limitations.
