# Stage 5 Review: task_tgid_nr helper

Patch: `stage5-task-tgid-nr-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`
- Directories: `include/linux`
- Public surface: `task_tgid_nr`, `is_global_init`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbols: `task_tgid_nr`, `is_global_init`
- Lite file: `include/linux/sched.h`
- This step only changes: add the Linux task TGID accessor as Lite's no-thread-group PID subset and route `is_global_init()` through it.

Mapping ledger:
- Functions:
  - `task_tgid_nr`: `linux2.6/include/linux/sched.h::task_tgid_nr`, lite=`include/linux/sched.h`, placement=OK
  - `is_global_init`: `linux2.6/include/linux/sched.h::is_global_init`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, the new helper uses the Linux symbol name.
- Placement: OK.
- Semantics: OK as a Lite subset; Lite has no thread groups, so TGID is PID.
- Flow/Lifetime: OK, accessor-only change.

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
- `git show -- include/linux/sched.h state.json Documentation/reviews/2026-04-30-stage5-task-tgid-nr-helper.md`

Findings: none.
