# Stage 5 Review: proc status adds Tgid

Patch: `stage5-proc-status-add-tgid`

## Linux Alignment Report

Change scope:
- Files: `fs/proc/array.c`
- Directories: `fs/proc`
- Public surface: `/proc/<pid>/status`

Reference-first evidence:
- Linux files: `linux2.6/fs/proc/array.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: status rendering, `task_tgid_nr`
- Lite file: `fs/proc/array.c`
- This step only changes: add the Linux `Tgid` status field using Lite's existing Linux-shaped `task_tgid_nr()` helper.

Mapping ledger:
- Functions:
  - status rendering: `linux2.6/fs/proc/array.c`, lite=`fs/proc/array.c::task_dump_status_pid`, placement=OK file, function shape=DIFF
  - `task_tgid_nr`: `linux2.6/include/linux/sched.h::task_tgid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/fs/proc/array.c`, lite=`fs/proc/array.c`, placement=OK
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/fs/proc`, lite=`fs/proc`, placement=OK
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, status field uses Linux `Tgid`.
- Placement: OK for the proc status surface.
- Semantics: OK as a Lite subset; TGID equals PID without thread groups.
- Flow/Lifetime: OK, read-only proc output addition.

If DIFF:
- Why: Lite status rendering is not split into Linux's exact helper shape yet.
- Impact: `/proc/<pid>/status` gains a Linux-compatible field; no task lifetime or scheduling behavior changes.
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
- `git show -- fs/proc/array.c state.json Documentation/reviews/2026-04-30-stage5-proc-status-add-tgid.md`

Findings: none.
