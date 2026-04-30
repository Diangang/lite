# Stage 5 Review: is_global_init helper

Patch: `stage5-is-global-init-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`
- Directories: `include/linux`
- Public surface: `is_global_init`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `is_global_init`
- Lite file: `include/linux/sched.h`
- This step only changes: add Linux's init-task predicate helper.

Mapping ledger:
- Functions:
  - `is_global_init`: `linux2.6/include/linux/sched.h::is_global_init`, lite=`include/linux/sched.h`, placement=OK
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
- Semantics: OK subset. Lite has no tgid/thread group yet, so PID 1 identifies global init.
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
- `git show -- include/linux/sched.h state.json Documentation/reviews/2026-04-30-stage5-is-global-init-helper.md`

Findings: none.
