Linux 2.6 Subsystem Alignment (Remaining Work)


## 文档定位
- 这是 **当前 Linux 2.6 对齐进度** 的权威跟踪文档。
- 它记录仍在推进的子系统、阶段状态和显式 DIFF；已完成内容只保留必要快照，不再作为完整实现手册。

This document tracks only the subsystems that are NOT fully aligned yet.
Subsystems that are already marked as DONE have been removed from this file to keep focus on remaining work.

Principles:
- No new concepts: every term/structure/interface must map to a Linux 2.6 concept.
- Naming must follow Linux vocabulary (e.g., kobject/kset, clockevents, sys_call_table, etc.).
- If a DIFF is unavoidable, document DIFF with Why/Impact/Plan and do not hide it behind ad-hoc behavior.
- Prefer staged convergence: Stage 0 report -> minimal Linux-shaped boundary -> expand semantics.


Progress Snapshot

Kernel / arch / driver core:
- kernel core (sched/fork/exit/wait/signal): `IN PROGRESS` (Stage 0 DONE; Stage 1 DONE; Stage 2 DONE; Stage 3 DONE; Stage 4 DONE; Stage 5 DONE; Stage 6 DONE; Stage 7 DONE; Stage 8 DONE; Stage 9 DONE; Stage 10 DONE; Stage 11 DONE; Stage 12 DONE; Stage 13 DONE; Stage 14 DONE; Stage 15+ pending)
- lib/: `IN PROGRESS` (Stage 0 DONE; Stage 1 DONE; Stage 2 DONE; Stage 3 DONE; Stage 4 DONE; Stage 5+ pending)
- arch/x86: `IN PROGRESS` (Stage 0 DONE; Stage 1 DONE; Stage 2 DONE; Stage 3 DONE; Stage 4 DONE; Stage 5 DONE; Stage 6 DONE; Stage 7 DONE; Stage 8 DONE; Stage 9 DONE; Stage 10 DONE; Stage 11 DONE; Stage 12 DONE; Stage 13 DONE; Stage 14+ pending)

arch/x86 Stage 3 DIFFs kept explicit:
- legacy PIC path: `DONE`
  - Linux mapping: `linux2.6/arch/x86/kernel/irq.c`, `linux2.6/arch/x86/kernel/i8259.c`
  - Status: Lite now has a minimal `irq_desc`/`irq_chip` boundary and a dedicated `i8259` layer; generic IRQ dispatch no longer embeds PIC remap/EOI logic.
- IRQ/vector coverage: `PARTIAL DIFF`
  - Linux mapping: x86 vector space and full legacy/APIC routing
  - Why: Lite now installs all legacy PIC `IRQ0-IRQ15` stubs and maps them through the existing `irq_desc`/`i8259` path, but still does not model Linux APIC/IOAPIC routing.
  - Impact: the 16-line legacy IRQ model is now internally consistent, but x86 vector management is still far simpler than Linux.
  - Plan: keep legacy PIC coverage complete; future expansion should target APIC/IOAPIC rather than adding new ad-hoc legacy paths.
- entry/interrupt low-level path: `PARTIAL DIFF`
  - Linux mapping: `linux2.6/arch/x86/entry/entry_32.S`
  - Why: Lite keeps a minimal `isr_common_stub` / `irq_common_stub` assembly path and a direct `int $0x20` yield path, without Linux's richer entry bookkeeping.
  - Impact: current entry code is adequate for UP smoke coverage, but does not model Linux's full entry layering, tracing hooks, or return-to-user/kernel distinctions.
  - Plan: keep the minimal stub model until broader entry/SMP work justifies a more Linux-shaped split.
- APIC / IOAPIC / SMP IRQ model: `DIFF`
  - Linux mapping: local APIC, IOAPIC, per-CPU irq stats, IPIs, `irq_enter()` / `irq_exit()`
  - Why: Lite currently targets a uniprocessor, legacy-PIC execution model.
  - Impact: no LAPIC timer, no reschedule/call-function IPIs, no APIC vector management, and no SMP irq accounting.
  - Plan: converge together with future SMP enablement rather than piecemeal in the legacy PIC path.
- irq core semantics: `PARTIAL DIFF`
  - Linux mapping: descriptor state machines, irq flow handlers, irqdomain/vector management
  - Why: Lite uses a deliberately small `irq_desc`/`irq_chip` subset with direct handler binding.
  - Impact: naming is Linux-shaped, but semantics are still far simpler than Linux's generic irq subsystem.
  - Plan: preserve naming and grow semantics only when additional controllers or SMP/vector management need them.
- regression note:
  - `make smoke-512` passed after one retry during this documentation-only stage, indicating an occasional early-boot/non-deterministic run rather than a deterministic regression from the arch/x86 Stage 3 documentation closure itself.

arch/x86 Stage 5 progress:
- legacy IRQ line -> vector mapping is now owned by `arch/x86/kernel/irq.c` instead of being hardcoded as a generic header formula.
- Linux mapping:
  - `linux2.6/arch/x86/kernel/irq.c`
  - `linux2.6/arch/x86/kernel/apic/apic.c`
  - `linux2.6/arch/x86/kernel/apic/io_apic.c`
- why this matters:
  - Linux does not treat vector assignment as a generic `IRQ_VECTOR_BASE + irq` contract.
  - keeping the mapping in arch code makes the current legacy PIC path explicit while leaving a Linux-shaped place for future APIC/IOAPIC vector allocation.
- current Lite semantics:
  - legacy PIC still uses the same vectors (`32..47`)
  - behavior is unchanged for current hardware paths
  - only the ownership boundary moved from header inline logic to arch-managed state
- remaining DIFF after Stage 5:
  - vector allocation is still static legacy-PIC mapping, not Linux APIC/vector management
  - there is still no LAPIC timer, IPI, or IOAPIC routing model

arch/x86 Stage 6 progress:
- explicit arch-owned LAPIC / IOAPIC placeholders now exist:
  - `arch/x86/kernel/apic.c`
  - `arch/x86/kernel/io_apic.c`
  - `include/asm/apic.h`
  - `include/asm/io_apic.h`
- Linux mapping:
  - `linux2.6/arch/x86/kernel/apic/apic.c`
  - `linux2.6/arch/x86/kernel/apic/io_apic.c`
- current Lite semantics:
  - `irq_install()` now passes through explicit LAPIC/IOAPIC placeholder init points before enabling the legacy `i8259` path
  - placeholders are no-op and keep APIC/IOAPIC disabled
  - generic IRQ helpers remain free of APIC/IOAPIC-specific assumptions
- impact:
  - user-visible interrupt behavior is unchanged
  - future APIC/IOAPIC bringup now has Linux-shaped arch-owned files instead of requiring new ad-hoc hooks in `irq.c` or `i8259.c`
- remaining DIFF after Stage 6:
  - no LAPIC timer
  - no IOAPIC routing
  - no IMCR/APIC mode switch
  - no IPI/SMP interrupt model

arch/x86 Stage 7 progress:
- interrupt controller mode ownership is now explicit:
  - `pic_mode` is arch-owned in `arch/x86/kernel/apic.c`
  - `irq_install()` explicitly requires PIC mode before entering the legacy `i8259` setup path
  - future APIC mode enablement now has a clear handoff point instead of being an implicit "not yet" assumption
- Linux mapping:
  - `pic_mode` in `linux2.6/arch/x86/kernel/apic/apic.c`
  - APIC-vs-PIC controller split owned by x86 arch code rather than generic IRQ helpers
- current Lite semantics:
  - boot always stays in PIC mode
  - if future code tries to leave PIC mode without implementing APIC routing, boot will fail loudly instead of silently mixing models
  - generic IRQ code still does not know APIC/IOAPIC details
- remaining DIFF after Stage 7:
  - PIC mode is explicit, but APIC mode is still unimplemented
  - no LAPIC timer, no IOAPIC routing, no IMCR switch, no IPI vectors

arch/x86 Stage 8 progress:
- Linux-shaped APIC/IPI vectors are now reserved explicitly in Lite arch code:
  - `LOCAL_TIMER_VECTOR`
  - `CALL_FUNCTION_VECTOR`
  - `RESCHEDULE_VECTOR`
  - `ERROR_APIC_VECTOR`
  - `SPURIOUS_APIC_VECTOR`
- Linux mapping:
  - `linux2.6/arch/x86/include/asm/irq_vectors.h`
  - `linux2.6/arch/x86/include/asm/entry_arch.h`
- current Lite semantics:
  - IDT entries exist for these vectors
  - assembly entry stubs exist for these vectors
  - all of them route to a placeholder handler that panics if triggered
  - PIC mode remains the only active runtime mode, so these entries stay dormant in normal boot/smoke execution
- impact:
  - future LAPIC timer and IPI work now has Linux-shaped vector ownership and entry points
  - generic IRQ helpers still do not need APIC-specific policy
- remaining DIFF after Stage 8:
  - vectors exist, but there is still no active LAPIC timer programming
  - no call-function/reschedule IPI send path
  - no IOAPIC routing

arch/x86 Stage 9 progress:
- APIC/IPI handler layering is now explicitly arch-owned:
  - `isr.c` still installs the IDT entries for APIC-shaped vectors
  - actual vector handler registration now happens via `apic_install_interrupts()`
  - `arch/x86/kernel/apic.c` owns the C-level handlers for:
    - `LOCAL_TIMER_VECTOR`
    - `RESCHEDULE_VECTOR`
    - `CALL_FUNCTION_VECTOR`
    - `ERROR_APIC_VECTOR`
    - `SPURIOUS_APIC_VECTOR`
- Linux mapping:
  - `linux2.6/arch/x86/include/asm/entry_arch.h`
  - `linux2.6/arch/x86/kernel/apic/apic.c`
- current Lite semantics:
  - PIC mode remains the only active runtime mode
  - if any APIC/IPI vector fires, control reaches an arch-owned handler path first
  - those handlers still fail fast because real LAPIC/IPI runtime semantics are not implemented
- impact:
  - vector ownership is no longer split between arch entry stubs and a generic placeholder in `isr.c`
  - future LAPIC timer / reschedule IPI / call-function IPI work now has a Linux-shaped arch landing point at both asm and C layers
- remaining DIFF after Stage 9:
  - no active LAPIC timer programming
  - no IPI send path
  - no IOAPIC routing

arch/x86 Stage 10 progress:
- APIC semantic boundaries are now encoded more explicitly in arch code:
  - `LOCAL_TIMER_VECTOR` uses a dedicated local-timer handler path
  - `RESCHEDULE_VECTOR` and `CALL_FUNCTION_VECTOR` use an IPI-specific handler path
  - `ERROR_APIC_VECTOR` and `SPURIOUS_APIC_VECTOR` use a local-APIC-event handler path
- Linux mapping:
  - `linux2.6/arch/x86/kernel/apic/apic.c` for local APIC timer / local APIC events
  - `linux2.6/arch/x86/kernel/smp.c` for reschedule and call-function IPI semantics
- current Lite semantics:
  - PIC mode remains the only active runtime mode
  - if one of these vectors fires unexpectedly, Lite now fails through a semantically distinct arch-owned path instead of a generic APIC placeholder
  - per-vector counters are also split along Linux-shaped semantic lines (`apic_timer_irqs`, `irq_resched_count`, `irq_call_count`, etc.)
- impact:
  - future LAPIC timer work can evolve independently from future IPI work
  - generic ISR code remains free of APIC policy
- remaining DIFF after Stage 10:
  - no active LAPIC timer programming
  - no IPI send path
  - no IOAPIC routing

arch/x86 Stage 11 progress:
- APIC-local event semantics are now split more precisely inside arch code:
  - `ERROR_APIC_VECTOR` uses a dedicated APIC error-event handler path
  - `SPURIOUS_APIC_VECTOR` uses a dedicated spurious-interrupt handler path
  - Lite records separate last-seen vectors for these two classes
- Linux mapping:
  - `linux2.6/arch/x86/kernel/apic/apic.c`
    - `__smp_error_interrupt()`
    - `__smp_spurious_interrupt()`
- current Lite semantics:
  - PIC mode remains the only active runtime mode
  - error vs spurious APIC events no longer share the same helper path
  - unexpected firing still fails fast, but now through a more Linux-shaped semantic split
- impact:
  - future APIC error handling can evolve independently from future spurious-vector handling
  - APIC-local event policy remains contained inside `arch/x86/kernel/apic.c`
- remaining DIFF after Stage 11:
  - no active LAPIC timer programming
  - no IPI send path
  - no IOAPIC routing

arch/x86 Stage 12 progress:
- IPI semantics are now split more precisely inside arch code:
  - `RESCHEDULE_VECTOR` uses a dedicated reschedule-IPI handler path
  - `CALL_FUNCTION_VECTOR` uses a dedicated call-function-IPI handler path
  - Lite records separate last-seen vectors for these two IPI classes
- Linux mapping:
  - `linux2.6/arch/x86/kernel/smp.c`
    - `__smp_reschedule_interrupt()`
    - `__smp_call_function_interrupt()`
- current Lite semantics:
  - PIC mode remains the only active runtime mode
  - reschedule vs call-function IPIs no longer share the same helper path
  - unexpected firing still fails fast, but now through a more Linux-shaped IPI split
- impact:
  - future scheduler-IPI work can evolve independently from future remote call-function work
  - APIC-local IPI policy remains contained inside `arch/x86/kernel/apic.c`
- remaining DIFF after Stage 12:
  - no active LAPIC timer programming
  - no IPI send path
  - no IOAPIC routing

arch/x86 Stage 13 progress:
- APIC install-time ownership is now split along the same semantic boundaries as handler-time ownership:
  - local timer vectors are installed through a dedicated local-timer install helper
  - IPI vectors are installed through a dedicated IPI install helper
  - APIC-local event vectors are installed through a dedicated local-event install helper
- Linux mapping:
  - `linux2.6/arch/x86/kernel/irqinit.c` for vector installation ownership
  - `linux2.6/arch/x86/kernel/apic/apic.c`
  - `linux2.6/arch/x86/kernel/smp.c`
- current Lite semantics:
  - PIC mode remains the only active runtime mode
  - install-time grouping now matches the semantic split already present in the handlers
  - generic ISR code still remains free of APIC policy
- impact:
  - future APIC/LAPIC work can extend install-time policy without reopening generic ISR code
  - arch-owned vector lifecycle is now clearer at both registration and handler layers
- remaining DIFF after Stage 13:
  - no active LAPIC timer programming
  - no IPI send path
  - no IOAPIC routing

kernel/sched + fork/exit/wait/signal Stage 3 DIFFs kept explicit:
- `tasklist_lock`: `PARTIAL DIFF`
  - Linux mapping: `tasklist_lock` / tasklist serialization in `linux2.6/kernel/fork.c`, `linux2.6/kernel/exit.c`, `linux2.6/kernel/signal.c`
  - Why: Lite currently maps this to `irq_save/irq_restore`, which is only a UP critical section, not Linux-style rwlock/spinlock serialization.
  - Impact: parent/child linkage, reparenting, reap, and pid lookup are only safe under single-CPU execution; SMP would need real lock ordering and remote visibility rules.
  - Plan: converge to a real SMP-safe tasklist lock once per-CPU scheduling/irq infrastructure exists.
- scheduler core globals: `PARTIAL DIFF`
  - Linux mapping: per-CPU `rq`, `current`, `TIF_NEED_RESCHED`, reschedule IPIs in `linux2.6/kernel/sched.c`
  - Why: Lite keeps a single global `current`, single global runqueue, and global `need_resched`.
  - Impact: scheduling state is globally serialized and cannot scale to SMP or preserve Linux per-CPU invariants.
  - Plan: split `current`/runqueue/resched state per CPU together with future SMP work.
- task lifetime / refs: `PARTIAL DIFF`
  - Linux mapping: `release_task()`, refcounted task lifetime, delayed final release paths
  - Why: Lite still frees `task_struct` directly after reap, without Linux-style task refs, RCU, or delayed put semantics.
  - Impact: current design is adequate for UP smoke coverage but would be fragile under parallel wait/kill/exit observers.
  - Plan: introduce explicit task refs/lifetime ownership together with SMP-safe wait/signal convergence.
- wait queues and wakeups: `PARTIAL DIFF`
  - Linux mapping: waitqueue head locking and wakeup discipline in Linux waitqueue core
  - Why: Lite wait queues are singly linked and protected only by local irq exclusion.
  - Impact: semantics are sufficient for current blocking/wakeup paths, but not for SMP-safe concurrent waiter/waker manipulation.
  - Plan: move waitqueue head protection to real spinlocks after SMP primitives land.
- signal model: `PARTIAL DIFF`
  - Linux mapping: `sigpending`, `signal_struct`, `sighand`, `siglock`, full kill/notify semantics
  - Why: Lite intentionally keeps a reduced signal model centered on exit reasons plus a minimal `kill()` subset.
  - Impact: Linux naming is roughly preserved, but semantics are intentionally much narrower than Linux 2.6.
  - Plan: keep minimal semantics for now; only expand when broader process model work requires it.

kernel/sched + fork/exit/wait/signal Stage 4 progress:
- target Linux mapping made explicit:
  - tasklist locking: `linux2.6/kernel/fork.c`, `linux2.6/kernel/exit.c`, `linux2.6/kernel/signal.c`
  - current/runqueue shape: `linux2.6/kernel/sched.c`
  - waitqueue discipline: Linux waitqueue core and `wait_chldexit` usage
  - task lifetime: `release_task()` and delayed final put semantics in Linux exit/reap paths
- lock ordering note kept explicit:
  - tasklist serialization must dominate parent/child linkage changes and zombie reaping decisions
  - waitqueue attachment/removal must not outlive the task visibility guaranteed by tasklist-linked ownership
  - mm/vfs teardown happens during exit/zombify before final task release, while final free is delayed until the task is no longer reachable
- concrete lifetime invariant:
  - a `task_struct` must not be freed while still reachable from `task_list_head`, any parent `children` list, or any waitqueue attachment
  - Lite now encodes this as a code-level release check via `task_release_invariant_holds()` before final free
- remaining DIFF after Stage 4:
  - the invariant is Linux-shaped, but enforcement still relies on UP-only exclusion instead of SMP-safe task refs/RCU/spinlocks
  - per-CPU runqueue/current shape is documented as the next convergence step, not fully implemented yet

kernel/sched + fork/exit/wait/signal Stage 5 progress:
- per-CPU shape introduced for the current UP scheduler state:
  - `boot_cpu_sched.current`
  - `boot_cpu_sched.rq`
  - `boot_cpu_sched.need_resched`
- compatibility with existing Lite call sites is preserved via:
  - `current` as a compatibility mirror of the boot CPU current task
  - `need_resched` as a compatibility mirror of the boot CPU resched bit
  - helper APIs: `task_current()`, `task_set_need_resched()`, `task_clear_need_resched()`, `task_need_resched()`
- Linux mapping:
  - per-CPU `current`
  - per-CPU runqueue `rq`
  - per-CPU resched ownership in Linux scheduler core
- impact:
  - user-visible UP behavior is unchanged
  - scheduler ownership is no longer modeled purely as one set of anonymous globals
  - future SMP/per-CPU work now has a Linux-shaped landing point
- remaining DIFF after Stage 5:
  - only CPU0 exists; there is no actual SMP scheduling
  - `current` and `need_resched` still remain exported as compatibility mirrors for existing code
  - runqueue locking/ownership is still UP-only

kernel/sched + fork/exit/wait/signal Stage 6 progress:
- compatibility mirrors are now explicitly treated as legacy surface:
  - owner state lives in `boot_cpu_sched.*`
  - `current` / `need_resched` remain as mirrors for existing call sites, but should not be treated as owner state
- enforcement by code structure:
  - scheduling decisions consult `task_current()` and `task_need_resched()`
  - resched requests use `task_set_need_resched()` / `task_clear_need_resched()`
  - direct writes to `need_resched` are eliminated outside of scheduler mirror sync
- Linux mapping:
  - per-CPU `current` / per-CPU resched ownership in `linux2.6/kernel/sched.c`
  - future reschedule IPI path remains the long-term convergence target
- remaining DIFF after Stage 6:
  - many existing call sites still read `current` directly; this is kept for churn control
  - the mirrors remain exported, so this is a convention + code-review rule until deeper refactoring

kernel/sched + fork/exit/wait/signal Stage 7 progress:
- one additional scheduler-adjacent path now avoids direct `current` usage:
  - waitqueue and `waitpid` paths consult `task_current()` to obtain the current task pointer, rather than relying on the exported compatibility mirror
- intent:
  - continue shrinking the "compatibility mirror is the API" surface area
  - keep ownership and lookup Linux-shaped for future per-CPU expansion
- remaining DIFF after Stage 7:
  - many call sites still read `current` directly across fs and driver code; those are out of scope for this core stage

kernel/sched + fork/exit/wait/signal Stage 8 progress:
- additional core paths now avoid direct reads of the exported `current` mirror:
  - `do_exit()` / `do_exit_reason()` consult `task_current()`
  - cred getters/setters consult `task_current()`
  - `sys_kill()` self-target logic consults `task_current()`
- intent:
  - reduce reliance on compatibility mirrors in core task/exit/signal/cred logic
  - keep "current task" ownership Linux-shaped for future per-CPU expansion
- remaining DIFF after Stage 8:
  - many call sites still read `current` directly outside the core scope (fs/drivers)
  - `current` remains exported as a compatibility mirror for churn control

kernel/sched + fork/exit/wait/signal Stage 9 progress:
- additional non-hot core lifecycle paths now avoid direct reads of the exported `current` mirror:
  - `task_create_internal()` derives parent ownership through `task_current()`
  - `fork_init()` validates scheduler bootstrap through `task_current()`
- intent:
  - keep shrinking compatibility-surface usage in core paths without touching the user `fork/exec` hot path
  - preserve a Linux-shaped current-task accessor in kernel-thread/bootstrap code
- remaining DIFF after Stage 9:
  - `sys_fork()` still reads `current` directly and remains an explicit stability exception for now
  - many non-core call sites still read `current` directly across fs/drivers

kernel/sched + fork/exit/wait/signal Stage 10 progress:
- the user `fork()` hot path now also derives current-task ownership through `task_current()`:
  - `sys_fork()` obtains `parent = task_current()`
  - child mm/fs/cred/parent/children linkage is then derived from `parent`
- impact:
  - kernel core no longer relies on direct `current` reads in `fork/exit/wait/signal/cred` paths outside of scheduler compatibility storage itself
  - the exported `current` symbol is now more clearly a compatibility mirror than a core API dependency
- remaining DIFF after Stage 10:
  - many non-core call sites still read `current` directly across fs/drivers
  - scheduler compatibility storage (`current` mirror in `sched.c`) remains exported for churn control

kernel/sched + fork/exit/wait/signal Stage 11 progress:
- remaining exported compatibility mirrors are now explicitly constrained as legacy scheduler surface:
  - `current` and `need_resched` are owned by `boot_cpu_sched.*`
  - `sched.c` keeps the exported mirrors synchronized for legacy callers
  - new core code is expected to use:
    - `task_current()`
    - `task_need_resched()`
    - `task_set_need_resched()`
    - `task_clear_need_resched()`
- direct-core exception set is now explicit:
  - `kernel/sched.c`
    - exported compatibility mirror storage (`current`, `need_resched`)
    - compatibility sync (`sched_sync_compat_globals()`)
- remaining DIFF after Stage 11:
  - non-core paths in fs/drivers may still read `current` directly
  - compatibility mirrors remain exported until a larger tree-wide cleanup becomes worthwhile

kernel/sched + fork/exit/wait/signal Stage 12 progress:
- legacy-surface constraints are now visible at the public scheduler interface:
  - `include/linux/sched.h` explicitly documents `current` and `need_resched` as compatibility mirrors
  - helper-based access remains the stated rule for new core code
- impact:
  - future core edits now hit the constraint at the declaration site, not only in `sched.c` internals or alignment notes
  - this reduces the chance of reintroducing direct core dependencies on exported compatibility mirrors
- remaining DIFF after Stage 12:
  - non-core paths in fs/drivers may still read `current` directly
  - compatibility mirrors remain exported for churn control and tree-wide compatibility

kernel/sched + fork/exit/wait/signal Stage 13 progress:
- helper-based scheduler ownership is now declared as the supported core access surface:
  - `include/linux/sched.h` explicitly marks:
    - `task_current()`
    - `task_set_need_resched()`
    - `task_clear_need_resched()`
    - `task_need_resched()`
  - these helpers are documented as the Linux-shaped core interface for current-task and resched ownership
- impact:
  - maintenance-mode guidance now exists at both declaration layers:
    - compatibility mirrors are marked as legacy
    - helper entry points are marked as the supported path
  - this further reduces the chance of future core code drifting back to direct mirror reads
- remaining DIFF after Stage 13:
  - non-core paths in fs/drivers may still read `current` directly
  - compatibility mirrors remain exported for compatibility and churn control

kernel/sched + fork/exit/wait/signal Stage 14 progress:
- current-task ownership writes are now funneled through a single scheduler helper:
  - `sched_set_current()` updates owner state in `boot_cpu_sched.current`
  - compatibility mirror sync then derives from that owner state
- current Lite effect:
  - task-switch and boot-task install paths no longer open-code separate current-owner assignment + mirror refresh sequences
  - owner-state writes stay visibly upstream of compatibility sync
- impact:
  - maintenance-mode ownership rules are now encoded not only in comments, but also in the write path shape inside `sched.c`
  - this further reduces drift back toward mirror-first scheduler state updates
- remaining DIFF after Stage 14:
  - non-core paths in fs/drivers may still read `current` directly
  - compatibility mirrors remain exported for compatibility and churn control

kernel/sched + fork/exit/wait/signal Stage 15 progress:
- boot-time resched initialization now also follows the helper-based owner path:
  - boot task install no longer open-codes `boot_cpu_sched.need_resched = 0`
  - it now uses `task_clear_need_resched()`
- current Lite effect:
  - boot initialization matches the same resched-owner update path used elsewhere in scheduler core
  - helper-based resched ownership now covers both runtime requests and boot-state initialization
- impact:
  - maintenance-mode scheduler convergence is now slightly tighter at the last obvious init-time write site
  - this further reduces drift back toward direct owner-field writes in core setup code
- remaining DIFF after Stage 15:
  - non-core paths in fs/drivers may still read `current` directly
  - compatibility mirrors remain exported for compatibility and churn control

lib/ Stage 1 DIFFs kept explicit:
- libc convenience helpers:
  - `strdup`: `DIFF`, kept as Lite convenience wrapper; kernel-facing call sites now use Linux-shaped `kstrdup`.
  - `itoa`: `DIFF`, kept only as a small internal helper; Linux mapping prefers `snprintf`-style formatting.
  - `printf`: `DIFF`, kept as a convenience wrapper over `vprintk`; Linux kernel-facing interface remains `printk`.
- `kref`: `PARTIAL DIFF`, naming/release-callback shape matches Linux, but refcounting is non-atomic and only valid under Lite's UP locking assumptions.
- `idr`: `PARTIAL DIFF`, API naming aligns, but implementation is still a flat growable slot array rather than Linux's radix-layered allocator.

lib/ Stage 2 progress:
- converted several kernel-facing text emitters from `itoa` to `snprintf`:
  - uevent text assembly
  - `/sys/kernel/*` text emitters
  - block sysfs numeric emitters
  - SCSI host/device/disk names
  - virtio/platform instance naming
  - procfs generic integer/hex append helpers
- `itoa` is now retained only as a Lite-local helper implementation in `lib/libc.c`; kernel call sites no longer depend on it.
- retained `printf` users are kept as an explicit long-term DIFF for:
  - early boot / bringup messages before broader `printk` cleanup
  - debug / diagnostic dumps in mm, nvme, pci, fs
  - a few convenience error/reporting paths that still wrap `vprintk`
- Linux mapping: retained `printf` is a Lite wrapper over `vprintk`, but Linux-facing interfaces should continue converging toward `printk` family calls.

lib/ Stage 3 progress:
- converted more kernel-facing error/reporting paths from `printf` to `printk`:
  - x86 exception / IRQ error reporting
  - `panic()` output
  - `fork()` allocation failure reporting
  - ELF/exec validation and allocation failure reporting
- retained `printf` categories are now explicit:
  - early boot / bringup / registration banners in init and core subsystem bringup
  - storage/mm/fs diagnostic dumps where multi-line ad-hoc tracing is still convenient during smoke bringup
  - smoke-oriented mount/filesystem debug traces
  - a few interactive or developer-facing convenience outputs such as scheduler/task listing and tty `^C` echo
- plan for retained `printf` users:
  - new kernel-facing error/reporting paths should prefer `printk`
  - early boot banners may stay Lite-local until a broader logging cleanup
  - diagnostic dump style call sites can be migrated module-by-module once `pr_*` style wrappers exist
- `kref` decision:
  - keep Linux-shaped API naming now
  - defer atomic/refcount semantic convergence to later SMP-focused work; current implementation remains an explicit UP-only DIFF
- `idr` decision:
  - keep the current flat slot-array implementation for now as a deliberate simplified backend
  - preserve Linux-shaped API surface; only converge the backend if scale/features require radix-like semantics

lib/ Stage 4 progress:
- kobject lifecycle teardown is now more Linux-shaped instead of being open-coded in scattered driver-core paths:
  - `lib/kobject.c` now provides `kobject_del()`
  - `device_unregister()`, `driver_unregister()`, and `subsystem_unregister()` now route root sysfs teardown through `kobject_del()`
  - `device_release_kobj()` no longer open-codes `sysfs_remove_dir()` in the final release callback
- parent/child lifetime shape improved:
  - `kobject_del()` now unlinks the object from the parent's child chain before clearing parent state
  - the device unregister path no longer needs a separate open-coded parent-child unlink just to tear down the kobject
- bus sysfs teardown is now grouped more explicitly:
  - bus subdir cleanup is funneled through a dedicated `bus_sysfs_unregister_subdirs()` helper
  - the bus root kobject itself is still removed through the generic kobject lifecycle path
- Linux mapping:
  - `linux2.6/lib/kobject.c`
  - Linux `kobject_del()` as the driver-core teardown boundary before the final `kobject_put()`
- remaining DIFF after Stage 4:
  - Lite still keeps a simplified child list and kset model; it does not implement Linux kernfs state/lifetime flags
  - `idr` backend remains a flat slot array, and `kref` remains UP-only


Acceptance Queue

Use this queue as the execution order. Each step is considered complete only when its acceptance criteria are all satisfied.
Step 1. `lib/` Stage 5
- Scope:
  - continue library lifecycle convergence around Linux-shaped driver-core helpers:
    - remove at least one additional open-coded teardown pattern around kobject/sysfs lifetime
    - keep semantics stable for current driver-core users
- Acceptance:
  - one additional driver-core teardown path stops open-coding kobject/sysfs removal
  - `make clean && make -j4 && make smoke-512` passes

Step 2. `arch/x86` Stage 14
- Scope:
  - continue APIC-shaped convergence without enabling APIC mode:
    - prepare the next arch-owned local APIC/IPI distinction that does not leak into generic ISR
- Acceptance:
  - one additional APIC-local semantic boundary is encoded in arch code
  - `make clean && make -j4 && make smoke-512` passes

Reference Mapping (vendored linux2.6/)

- input/tty/console:
  - `linux2.6/drivers/tty/tty_io.c`, `linux2.6/drivers/tty/n_tty.c`
  - `linux2.6/drivers/input/serio/i8042.c`, `linux2.6/drivers/input/keyboard/atkbd.c`
- kernel/sched+fork/exit/wait/signal:
  - `linux2.6/kernel/sched.c`, `linux2.6/kernel/fork.c`, `linux2.6/kernel/exit.c`, `linux2.6/kernel/signal.c`
- arch/x86:
  - `linux2.6/arch/x86/kernel/entry_32.S`, `linux2.6/arch/x86/kernel/irq.c`
  - `linux2.6/arch/x86/kernel/i8259.c` (PIC/irqchip reference)
- lib/:
  - `linux2.6/lib/string.c`, `linux2.6/lib/vsprintf.c`, `linux2.6/lib/kref.c`, `linux2.6/lib/idr.c`
