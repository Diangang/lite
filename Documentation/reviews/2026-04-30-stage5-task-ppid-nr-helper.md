# Stage 5 Review: task_ppid_nr helper

Patch: `stage5-task-ppid-nr-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`
- Directories: `include/linux`
- Public surface: `task_ppid_nr`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `task_ppid_nr`
- Lite file: `include/linux/sched.h`
- This step only changes: add Linux's parent-PID accessor over `real_parent`.

Mapping ledger:
- Functions:
  - `task_ppid_nr`: `linux2.6/include/linux/sched.h::task_ppid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, helper name matches Linux.
- Placement: OK.
- Semantics: OK subset. Lite has no pid namespace, so the helper returns `real_parent->pid` or 0.
- Flow/Lifetime: OK, accessor only.

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
- `git show -- include/linux/sched.h state.json Documentation/reviews/2026-04-30-stage5-task-ppid-nr-helper.md`

Findings: none.
