# Stage 5 Review: proc status uses get_task_comm

Patch: `stage5-proc-status-uses-get-task-comm`

## Linux Alignment Report

Change scope:
- Files: `fs/proc/array.c`
- Directories: `fs/proc`
- Public surface: `/proc/<pid>/status` name rendering

Reference-first evidence:
- Linux files: `linux2.6/fs/proc/array.c`, `linux2.6/fs/exec.c`
- Linux symbols: `task_name`, `get_task_comm`
- Lite file: `fs/proc/array.c`
- This step only changes: `/proc/<pid>/status` Name field reads `comm` through `get_task_comm()` instead of direct field access.

Mapping ledger:
- Functions:
  - `task_name`: `linux2.6/fs/proc/array.c::task_name`, lite status rendering=`fs/proc/array.c::task_dump_status_pid`, placement=OK file, function shape=DIFF
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
- Why: Lite status rendering is not split into Linux's exact `task_name()` helper shape yet.
- Impact: no proc format, task lifetime, or locking behavior changes.
- Plan: handle broader proc array shape convergence separately.

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
- `git show -- fs/proc/array.c state.json Documentation/reviews/2026-04-30-stage5-proc-status-uses-get-task-comm.md`

Findings: none.
