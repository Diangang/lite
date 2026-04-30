# Stage 5 Review: current id uses task_tgid_nr

Patch: `stage5-current-id-uses-task-tgid-nr`

## Linux Alignment Report

Change scope:
- Files: `kernel/sched/core.c`
- Directories: `kernel/sched`
- Public surface: `task_get_current_id`

Reference-first evidence:
- Linux files: `linux2.6/kernel/sys.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: `sys_getpid`, `task_tgid_vnr`, `task_tgid_nr`
- Lite file: `kernel/sched/core.c`
- This step only changes: route the existing current-task ID helper through the Linux TGID accessor shape instead of open-coding `task->pid`.

Mapping ledger:
- Functions:
  - `sys_getpid`: `linux2.6/kernel/sys.c::sys_getpid`, lite dispatch=`arch/x86/entry/syscall_32.c::sys_getpid_dispatch`, placement=DIFF
  - `task_get_current_id`: Lite helper for syscall dispatch, lite=`kernel/sched/core.c`, placement=NO_DIRECT_LINUX_MATCH
  - `task_tgid_nr`: `linux2.6/include/linux/sched.h::task_tgid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/kernel/sys.c`, lite syscall dispatch remains architecture-local, placement=DIFF
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/kernel`, lite helper currently under `kernel/sched`, placement=DIFF
- NO_DIRECT_LINUX_MATCH:
  - `task_get_current_id`: existing Lite compatibility helper, not a new abstraction.

Consistency:
- Naming: DIFF for the existing Lite helper name.
- Placement: DIFF for the existing syscall/helper split.
- Semantics: OK as a Lite subset; `getpid` returns current TGID, and TGID is PID without thread groups.
- Flow/Lifetime: OK, accessor-only caller change.

If DIFF:
- Why: this patch does not move syscall implementation placement; it only removes direct PID field access from the existing helper.
- Impact: no syscall number, ABI, task lifetime, or scheduling behavior changes.
- Plan: leave broader syscall placement cleanup for a dedicated roadmap patch.

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
- `git show -- kernel/sched/core.c state.json Documentation/reviews/2026-04-30-stage5-current-id-uses-task-tgid-nr.md`

Findings: none.
