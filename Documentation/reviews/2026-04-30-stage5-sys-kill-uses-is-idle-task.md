# Stage 5: sys_kill uses is_idle_task

## Linux Alignment Report

Change scope:
- files: `kernel/signal.c`
- directories: `kernel`
- public surface: `kill()` target validation for the idle task

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `is_idle_task`
- Linux evidence: idle task identity is represented by `is_idle_task(p)`, which returns true when `p->pid == 0`.
- Linux file: `linux2.6/kernel/signal.c`
- Linux evidence: signal paths use task helpers such as `is_global_init()` for special task identity decisions.
- Lite file: `kernel/signal.c`
- This step only changes: replace the direct idle PID check in `sys_kill()` with `is_idle_task(t)`.

Mapping ledger:
- functions:
  - `sys_kill`: Linux signal path=`linux2.6/kernel/signal.c::{kill_pid, kill_pid_info, group_send_sig_info}`, lite=`kernel/signal.c::sys_kill`, placement=OK
  - `is_idle_task`: Linux=`linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK
- structs:
  - `task_struct`: Linux=`linux2.6/include/linux/sched.h::task_struct`, lite=`include/linux/sched.h::task_struct`, placement=OK
- globals/statics: none
- files:
  - `kernel/signal.c`: placement=OK
- directories:
  - `kernel`: placement=OK
- NO_DIRECT_LINUX_MATCH: none for the helper substitution.

Consistency:
- Naming: OK, uses Linux helper name.
- Placement: OK.
- Semantics: OK, behavior remains rejecting pid-0 idle task as a kill target.
- Flow/Lifetime: OK, no locking or signal-delivery order changes.

## Patch Summary

- Changed `sys_kill()` target validation from `t->pid == 0` to `is_idle_task(t)`.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Findings: none.

Residual risk:
- None specific to this helper substitution; it preserves the same predicate while removing a direct PID identity check.
