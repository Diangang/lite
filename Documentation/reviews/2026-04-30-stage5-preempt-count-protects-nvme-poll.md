# Stage 5: preempt_count protects NVMe poll

## Linux Alignment Report

Change scope:
- files: `include/linux/sched.h`, `kernel/sched/core.c`, `arch/x86/kernel/irq.c`, `drivers/nvme/host/pci.c`
- directories: `include/linux`, `kernel/sched`, `arch/x86/kernel`, `drivers/nvme/host`
- public surface: internal scheduler preemption guard and NVMe synchronous poll behavior

Reference-first evidence:
- Linux file: `linux2.6/arch/x86/entry/entry_32.S`
- Linux symbols: `ret_from_intr`, `resume_kernel`
- Linux evidence: interrupt return to kernel mode goes through `resume_kernel`; with preemption enabled it checks `__preempt_count` before calling `preempt_schedule_irq`, and without kernel preemption it restores state rather than scheduling from arbitrary kernel IRQ context.
- Linux file: `linux2.6/kernel/sched/core.c`
- Linux symbols: `preempt_count`, `preempt_schedule_irq`
- Lite files: `arch/x86/kernel/irq.c`, `kernel/sched/core.c`, `include/linux/sched.h`, `drivers/nvme/host/pci.c`
- This step only changes: add a minimal Linux-shaped preempt count gate so timer IRQs keep advancing jiffies but do not schedule away from the synchronous NVMe poll critical section.

Mapping ledger:
- functions:
  - `preempt_disable`: Linux=`linux2.6/include/linux/preempt.h::preempt_disable`, lite=`kernel/sched/core.c::preempt_disable`, placement=DIFF
  - `preempt_enable`: Linux=`linux2.6/include/linux/preempt.h::preempt_enable`, lite=`kernel/sched/core.c::preempt_enable`, placement=DIFF
  - `preempt_count`: Linux=`linux2.6/kernel/sched/core.c::preempt_count usage`, lite=`kernel/sched/core.c::preempt_count`, placement=OK for current Lite scheduler subset
  - `irq_handler`: Linux flow=`linux2.6/arch/x86/entry/entry_32.S::ret_from_intr/resume_kernel`, lite=`arch/x86/kernel/irq.c::irq_handler`, placement=OK for Lite IRQ dispatch
  - `nvme_cq_poll_cid`: Linux equivalent polling/completion handling=`linux2.6/drivers/nvme/host/*`, lite=`drivers/nvme/host/pci.c::nvme_cq_poll_cid`, placement=OK
- structs: none
- globals/statics:
  - `kernel_preempt_count`: Linux concept=`preempt_count`, lite=`kernel/sched/core.c`, placement=OK for current scheduler-owned subset
- files:
  - IRQ return policy remains in `arch/x86/kernel/irq.c`.
  - scheduler-owned preempt counter remains in `kernel/sched/core.c`.
  - NVMe poll guard remains in `drivers/nvme/host/pci.c`.
- directories: placement OK for Lite's current simplified layout.
- NO_DIRECT_LINUX_MATCH: none for the concept; Lite keeps a minimal subset.

Consistency:
- Naming: OK, uses Linux preempt vocabulary.
- Placement: DIFF for the full Linux header split; accepted because Lite does not yet have `include/linux/preempt.h`.
- Semantics: OK, timer IRQs still tick while preempt-disabled regions defer scheduler switching.
- Flow/Lifetime: OK, no task lifetime changes.

If DIFF:
- Why: Lite has a compact scheduler header/source split and lacks the full Linux preempt header.
- Impact: provides the missing scheduler gate needed after syscall dispatch was made IRQ-on.
- Plan: keep this as a minimal scheduler-owned preempt counter until a later broader scheduler/preempt header pass.

## Root Cause

After aligning `int 0x80` syscall dispatch to keep IRQs enabled, timer IRQs can arrive while a syscall is executing kernel code. Lite's timer IRQ path immediately called `task_schedule()` whenever `need_resched` was set. That allowed scheduling from inside synchronous NVMe polling, unlike Linux's kernel-mode interrupt return path which gates kernel preemption with `preempt_count`.

The observed failure was:
- `make smoke-512`
- NVMe I/O timeout `err=-110`
- later smoke harness timeout when the NVMe/minix path could not complete

## Patch Summary

- Added minimal `preempt_disable()`, `preempt_enable()`, and `preempt_count()` scheduler helpers.
- Made timer IRQ scheduling check `preempt_count() == 0`.
- Wrapped `nvme_cq_poll_cid()` with `preempt_disable()` / `preempt_enable()` so jiffies continue advancing while scheduler switching is deferred.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed
- `make smoke-512`: passed again

## Review

Findings: none.

Residual risk:
- The preempt counter is a minimal single-CPU subset. It is intentionally not a full Linux preemption implementation.
