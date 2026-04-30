# Stage 5 Review: task_create_internal returns task_pid_nr

Patch: `stage5-task-create-internal-returns-task-pid-nr`

## Linux Alignment Report

Change scope:
- Files: `kernel/fork.c`
- Directories: `kernel`
- Public surface: `kernel_thread` and `kernel_create_user` return values

Reference-first evidence:
- Linux files: `linux2.6/kernel/fork.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: `kernel_thread`, `task_pid_nr`
- Lite file: `kernel/fork.c`
- This step only changes: `task_create_internal()` returns the new task PID through `task_pid_nr()` instead of direct field access.

Mapping ledger:
- Functions:
  - `kernel_thread`: `linux2.6/kernel/fork.c::kernel_thread`, lite=`kernel/fork.c::kernel_thread`, placement=OK
  - `task_create_internal`: Lite helper for `kernel_thread` and `kernel_create_user`, lite=`kernel/fork.c`, placement=NO_DIRECT_LINUX_MATCH
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/kernel/fork.c`, lite=`kernel/fork.c`, placement=OK
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/kernel`, lite=`kernel`, placement=OK
- NO_DIRECT_LINUX_MATCH:
  - `task_create_internal`: existing Lite helper, not a new abstraction.

Consistency:
- Naming: OK for the Linux helper use.
- Placement: OK for the fork file.
- Semantics: OK, return value remains the new task PID.
- Flow/Lifetime: OK, return-value-only change.

If DIFF:
- Why: Lite shares a compact internal helper across kernel and initial user task creation.
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
- `git show -- kernel/fork.c state.json Documentation/reviews/2026-04-30-stage5-task-create-internal-returns-task-pid-nr.md`

Findings: none.
