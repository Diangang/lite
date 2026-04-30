# Stage 5 Review: Release Task Uses Is Idle Task

## Scope

- Patch: `stage5-release-task-uses-is-idle-task`
- Lite file: `kernel/exit.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/exit.c`
- directories: `kernel`
- public surface: none; internal release guard only.

Reference-first evidence:
- Linux `release_task()` is the corresponding task release path in `kernel/exit.c`.
- Linux defines `is_idle_task(const struct task_struct *p)` in `include/linux/sched.h` for pid-zero idle task checks.
- Lite `release_task()` already checks for a null task before the idle-task guard.
- This step only changes `release_task()` from direct `task->pid == 0` to `is_idle_task(task)`.

Mapping ledger:
- functions:
  - `release_task`: `linux2.6/kernel/exit.c::release_task`, lite=`kernel/exit.c::release_task`, placement=OK.
  - `is_idle_task`: `linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none for release path or guard concept.

Consistency:
- Naming: OK, uses existing Linux-named `is_idle_task()`.
- Placement: OK.
- Semantics: OK, idle task is still not released.
- Flow/Lifetime: OK, tasklist unlink and refcount ordering are unchanged.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: Remaining pid-zero guards in `do_exit()`, `do_exit_reason()`, and `task_release_invariant_holds()` remain separate patch candidates.
