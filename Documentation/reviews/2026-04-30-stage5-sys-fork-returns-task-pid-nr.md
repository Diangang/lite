# Stage 5 Review: sys_fork returns task_pid_nr

Patch: `stage5-sys-fork-returns-task-pid-nr`

## Linux Alignment Report

Change scope:
- Files: `kernel/fork.c`
- Directories: `kernel`
- Public surface: `fork` return value

Reference-first evidence:
- Linux files: `linux2.6/kernel/fork.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: `_do_fork`, `task_pid_nr`
- Lite file: `kernel/fork.c`
- This step only changes: `sys_fork()` returns the child PID through `task_pid_nr()` instead of direct field access.

Mapping ledger:
- Functions:
  - fork path: `linux2.6/kernel/fork.c::_do_fork`, lite=`kernel/fork.c::sys_fork`, placement=OK file, function shape=DIFF
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/kernel/fork.c`, lite=`kernel/fork.c`, placement=OK
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/kernel`, lite=`kernel`, placement=OK
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, caller uses the Linux helper name.
- Placement: OK.
- Semantics: OK, fork still returns the child PID.
- Flow/Lifetime: OK, return-value-only change.

If DIFF:
- Why: Lite fork is a small direct `sys_fork()` path rather than Linux's full `_do_fork()`/`copy_process()` split.
- Impact: no ABI value change, task lifetime change, or scheduling behavior change.
- Plan: handle broader fork path convergence separately.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- kernel/fork.c state.json Documentation/reviews/2026-04-30-stage5-sys-fork-returns-task-pid-nr.md`

Findings: none.
