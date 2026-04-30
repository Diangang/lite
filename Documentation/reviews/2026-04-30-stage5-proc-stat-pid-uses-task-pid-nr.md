# Stage 5 Review: proc stat PID uses task_pid_nr

Patch: `stage5-proc-stat-pid-uses-task-pid-nr`

## Linux Alignment Report

Change scope:
- Files: `fs/proc/array.c`
- Directories: `fs/proc`
- Public surface: `/proc/<pid>/stat`

Reference-first evidence:
- Linux files: `linux2.6/fs/proc/array.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: stat rendering, `task_pid_nr`
- Lite file: `fs/proc/array.c`
- This step only changes: stat PID output reads through `task_pid_nr()` instead of direct `t->pid`.

Mapping ledger:
- Functions:
  - stat rendering: `linux2.6/fs/proc/array.c`, lite=`fs/proc/array.c::task_dump_stat_pid`, placement=OK file, function shape=DIFF
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/fs/proc/array.c`, lite=`fs/proc/array.c`, placement=OK
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/fs/proc`, lite=`fs/proc`, placement=OK
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, caller uses the Linux helper name.
- Placement: OK for the proc stat surface.
- Semantics: OK, value remains PID for Lite tasks.
- Flow/Lifetime: OK, read-only caller change.

If DIFF:
- Why: Lite stat rendering is not split into Linux's exact helper shape yet.
- Impact: no proc format, task lifetime, or scheduling behavior changes.
- Plan: handle broader proc array shape convergence separately.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed on rerun

Note:
- The first `make smoke-512` attempt hit the smoke script's 30 second hard timeout during NVMe minix mount+rw, without a smoke `FAIL` summary. The same command passed on immediate rerun.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- fs/proc/array.c state.json Documentation/reviews/2026-04-30-stage5-proc-stat-pid-uses-task-pid-nr.md`

Findings: none.
