# Stage 5 Review: Task Zombify Uses Is Idle Task

## Scope

- Patch: `stage5-task-zombify-uses-is-idle-task`
- Lite file: `kernel/exit.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/exit.c`
- directories: `kernel`
- public surface: none; internal task lifecycle guard only.

Reference-first evidence:
- Linux defines `is_idle_task(const struct task_struct *p)` in `include/linux/sched.h` for pid-zero idle task checks.
- Linux `do_exit()` also treats pid zero as the idle task.
- Lite `task_zombify()` already checks for a null task before the idle-task guard.
- This step only changes `task_zombify()` from direct `task->pid == 0` to `is_idle_task(task)`.

Mapping ledger:
- functions:
  - `is_idle_task`: `linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK.
  - `task_zombify`: no direct Linux symbol; lite=`kernel/exit.c::task_zombify`, placement=NO_DIRECT_LINUX_MATCH.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH:
  - Why: Lite splits zombie state setup into a compact helper while Linux folds this lifecycle into the broader exit path.
  - Impact: The patch changes only idle-task guard spelling, not lifecycle ordering.
  - Plan: Convert remaining pid-zero guards separately where the Linux idle-task concept applies.

Consistency:
- Naming: OK, uses existing Linux-named `is_idle_task()`.
- Placement: OK for helper; Lite-local lifecycle helper remains in its existing file.
- Semantics: OK, idle task still returns without zombification.
- Flow/Lifetime: OK, no ordering changes.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: Other direct pid-zero guards in `kernel/exit.c` remain separate patch candidates.
