# Stage 5 Review: Wait Self Check Uses Task PID Helper

## Scope

- Patch: `stage5-wait-self-check-uses-task-pid-nr`
- Lite file: `kernel/sched/wait.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/sched/wait.c`
- directories: `kernel/sched`
- public surface: `waitpid(id)` self-id guard

Reference-first evidence:
- Linux `sys_wait4()` resolves positive pid waits through `find_get_pid()` and later `eligible_pid()`/`task_pid_type()`, avoiding direct raw pid reads in wait target matching.
- Lite has a compact self-id guard before scanning children.
- This step only changes the self-id guard's pid access from `task->pid` to `task_pid_nr(task)`.

Mapping ledger:
- functions:
  - `sys_wait4/eligible_pid`: `linux2.6/kernel/exit.c::sys_wait4/eligible_pid`, lite=`kernel/sched/wait.c::do_waitpid`, placement=DIFF because Lite keeps compact wait handling in `kernel/sched/wait.c`.
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr/task_pid_vnr`, lite=`include/linux/sched.h::task_pid_nr`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none for helper-based pid access.

Consistency:
- Naming: OK, uses existing Linux-named `task_pid_nr()`.
- Placement: DIFF, accepted as existing Lite wait placement; no file movement in this patch.
- Semantics: OK, Lite's existing self-id rejection is preserved.
- Flow/Lifetime: OK, no waitqueue, tasklist lock, or reaping behavior changed.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: This patch intentionally does not change Linux/Lite differences in wait option handling or pid namespace behavior.
