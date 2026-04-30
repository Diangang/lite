# Stage 5 Review: find_task_by_vpid uses task_pid_nr

Patch: `stage5-find-task-by-vpid-uses-task-pid-nr`

## Linux Alignment Report

Change scope:
- Files: `kernel/pid.c`
- Directories: `kernel`
- Public surface: internal PID lookup

Reference-first evidence:
- Linux files: `linux2.6/kernel/pid.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: `find_task_by_vpid`, `task_pid_nr`
- Lite file: `kernel/pid.c`
- This step only changes: Lite's linear PID lookup compares through `task_pid_nr()` instead of direct `t->pid`.

Mapping ledger:
- Functions:
  - `find_task_by_vpid`: `linux2.6/kernel/pid.c::find_task_by_vpid`, lite=`kernel/pid.c`, placement=OK
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/kernel/pid.c`, lite=`kernel/pid.c`, placement=OK
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/kernel`, lite=`kernel`, placement=OK
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, existing symbol names match Linux.
- Placement: OK.
- Semantics: OK as a Lite no-namespace subset.
- Flow/Lifetime: OK, comparison-only change.

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
- `git show -- kernel/pid.c state.json Documentation/reviews/2026-04-30-stage5-find-task-by-vpid-uses-task-pid-nr.md`

Findings: none.
