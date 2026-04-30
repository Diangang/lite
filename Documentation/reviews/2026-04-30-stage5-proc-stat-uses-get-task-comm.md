# Stage 5 Review: proc stat uses get_task_comm

Patch: `stage5-proc-stat-uses-get-task-comm`

## Linux Alignment Report

Change scope:
- Files: `fs/proc/array.c`
- Directories: `fs/proc`
- Public surface: `/proc/<pid>/stat` command-name rendering

Reference-first evidence:
- Linux files: `linux2.6/fs/proc/array.c`, `linux2.6/fs/exec.c`
- Linux symbols: proc stat rendering, `get_task_comm`
- Lite file: `fs/proc/array.c`
- This step only changes: `/proc/<pid>/stat` command name reads `comm` through `get_task_comm()` instead of direct field access.

Mapping ledger:
- Functions:
  - proc stat rendering: `linux2.6/fs/proc/array.c`, lite=`fs/proc/array.c::task_dump_stat_pid`, placement=OK file, function shape=DIFF
  - `get_task_comm`: `linux2.6/fs/exec.c::get_task_comm`, lite=`kernel/fork.c`, placement=DIFF from the documented helper patch
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/fs/proc/array.c`, lite=`fs/proc/array.c`, placement=OK
- Directories:
  - `linux2.6/fs/proc`, lite=`fs/proc`, placement=OK
- NO_DIRECT_LINUX_MATCH: none for accessor use.

Consistency:
- Naming: OK, caller uses the Linux helper name.
- Placement: OK for the proc array caller.
- Semantics: OK, output fallback remains `-` for empty comm.
- Flow/Lifetime: OK, local stack-copy caller change.

If DIFF:
- Why: Lite stat rendering is not split into Linux's exact helper shape yet.
- Impact: no proc format, task lifetime, or locking behavior changes.
- Plan: handle broader proc array shape convergence separately.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed on rerun
- `make smoke-512`: passed on rerun

Note:
- The first `make smoke-128` and first `make smoke-512` attempts hit the same NVMe mount read timeout before the smoke suite started: `nvme: io read failed lba=8 nlb=2 len=1024 err=-110`, followed by `vfs_get_sb_single: fill_super failed`. Each command passed on immediate rerun.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- fs/proc/array.c state.json Documentation/reviews/2026-04-30-stage5-proc-stat-uses-get-task-comm.md`

Findings: none.
