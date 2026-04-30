# Stage 5: task release invariant uses is_idle_task

## Linux Alignment Report

Change scope:
- files: `include/linux/sched.h`
- directories: `include/linux`
- public surface: internal task helper usage in the Lite task release invariant

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `is_idle_task`
- Linux evidence: `is_idle_task(const struct task_struct *p)` returns true when `p->pid == 0`.
- Lite file: `include/linux/sched.h`
- This step only changes: replace the direct idle PID check in `task_release_invariant_holds()` with the existing Linux-shaped `is_idle_task()` helper.

Mapping ledger:
- functions:
  - `is_idle_task`: Linux=`linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK
  - `task_release_invariant_holds`: Linux=`NO_DIRECT_LINUX_MATCH`, lite=`include/linux/sched.h::task_release_invariant_holds`, placement=NO_DIRECT_LINUX_MATCH
- structs:
  - `task_struct`: Linux=`linux2.6/include/linux/sched.h::task_struct`, lite=`include/linux/sched.h::task_struct`, placement=OK
- globals/statics: none
- files:
  - `include/linux/sched.h`: placement=OK
- directories:
  - `include/linux`: placement=OK
- NO_DIRECT_LINUX_MATCH:
  - `task_release_invariant_holds` is a Lite invariant helper around local release safety checks.

Consistency:
- Naming: OK, uses Linux helper name.
- Placement: OK for `is_idle_task`; NO_DIRECT_LINUX_MATCH for the Lite invariant helper.
- Semantics: OK, idle detection remains `pid == 0`.
- Flow/Lifetime: OK, no release order changes.

If DIFF:
- Why: Linux has richer task lifetime machinery; Lite carries a local invariant helper for release guards.
- Impact: direct idle PID logic is replaced with the Linux-shaped helper without changing the invariant result.
- Plan: keep the local invariant helper but express idle-task checks through `is_idle_task()`.

## Patch Summary

- Moved `is_idle_task()` above `task_release_invariant_holds()` so the invariant can call it.
- Changed the invariant's direct `task->pid == 0` check to `is_idle_task(task)`.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed
- `make smoke-512`: passed again after the syscall IRQ timeout fix

## Review

Findings: none.

Residual risk:
- None specific to this helper substitution; it is behavior-preserving because Lite's `is_idle_task()` is the same `pid == 0` predicate.
