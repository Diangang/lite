# Stage 5 Review: Add Is Idle Task Helper

## Scope

- Patch: `stage5-add-is-idle-task-helper`
- Lite file: `include/linux/sched.h`
- Linux reference:
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `include/linux/sched.h`
- directories: `include/linux`
- public surface: internal kernel helper only, no syscall or user ABI change.

Reference-first evidence:
- Linux defines `is_idle_task(const struct task_struct *p)` in `include/linux/sched.h`.
- The Linux helper checks whether the task pid is zero.
- This step only adds the Linux-named helper to Lite and does not change any call sites.

Mapping ledger:
- functions:
  - `is_idle_task`: `linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, exact Linux helper name.
- Placement: OK, same corresponding header.
- Semantics: OK, idle task remains pid 0.
- Flow/Lifetime: OK, no behavior change in this patch.

## Validation

- `make -j4`: passed
  - Existing warning observed in `arch/x86/kernel/traps.c` about array subscript bounds.
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: Direct `pid == 0` call sites remain for separate narrow conversion patches.
