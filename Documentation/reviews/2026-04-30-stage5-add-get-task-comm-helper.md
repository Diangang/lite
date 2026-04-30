# Stage 5 Review: add get_task_comm helper

Patch: `stage5-add-get-task-comm-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`, `kernel/fork.c`
- Directories: `include/linux`, `kernel`
- Public surface: `get_task_comm`

Reference-first evidence:
- Linux files: `linux2.6/include/linux/sched.h`, `linux2.6/fs/exec.c`
- Linux symbol: `get_task_comm`
- Lite files: `include/linux/sched.h`, `kernel/fork.c`
- This step only changes: add the Linux-named task command accessor; no proc or user-visible output behavior changes.

Mapping ledger:
- Functions:
  - `get_task_comm`: `linux2.6/fs/exec.c::get_task_comm`, lite=`kernel/fork.c`, placement=DIFF
  - `set_task_comm`: `linux2.6/include/linux/sched.h::set_task_comm`, lite=`kernel/fork.c`, placement=DIFF
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, declaration placement=OK
  - `linux2.6/fs/exec.c`, lite implementation currently in `kernel/fork.c`, placement=DIFF
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
  - `linux2.6/fs`, lite implementation currently in `kernel`, placement=DIFF
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, the helper uses the Linux symbol name.
- Placement: DIFF, implementation follows Lite's existing `set_task_comm()` location.
- Semantics: OK as a Lite subset; the helper copies `comm` into the caller buffer and returns that buffer.
- Flow/Lifetime: OK, accessor-only addition.

If DIFF:
- Why: this patch does not move existing comm ownership; it adds the missing accessor beside Lite's current `set_task_comm()`.
- Impact: no ABI, proc output, task lifetime, or exec behavior changes.
- Plan: handle any broader exec/fork placement cleanup as a dedicated roadmap patch.

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
- `git show -- include/linux/sched.h kernel/fork.c state.json Documentation/reviews/2026-04-30-stage5-add-get-task-comm-helper.md`

Findings: none.
