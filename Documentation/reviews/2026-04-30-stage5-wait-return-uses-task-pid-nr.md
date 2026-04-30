# Stage 5 Review: Wait Return Uses Task PID Helper

## Scope

- Patch: `stage5-wait-return-uses-task-pid-nr`
- Lite file: `kernel/sched/wait.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/sched/wait.c`
- directories: `kernel/sched`
- public surface: `sys_waitpid()` returned pid after reaping a zombie child

Reference-first evidence:
- Linux `wait_task_zombie()` caches `pid_t pid = task_pid_vnr(p)` before returning the reaped child's pid.
- Linux `wait_task_stopped()` and `wait_task_continued()` also use `task_pid_vnr(p)` for wait-visible pid reporting.
- Lite already has `task_pid_nr()` in `include/linux/sched.h`.
- This step only changes Lite's zombie-child wait return value from direct `t->pid` to `task_pid_nr(t)`.

Mapping ledger:
- functions:
  - `wait_task_zombie`: `linux2.6/kernel/exit.c::wait_task_zombie`, lite=`kernel/sched/wait.c::do_waitpid`, placement=DIFF because Lite keeps compact wait handling in `kernel/sched/wait.c`.
  - `task_pid_vnr/task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_vnr/task_pid_nr`, lite=`include/linux/sched.h::task_pid_nr`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none for the task pid helper concept.

Consistency:
- Naming: OK, uses existing Linux-named `task_pid_nr()`.
- Placement: DIFF, accepted as existing Lite wait placement; no file movement in this patch.
- Semantics: OK, wait-visible child pid now goes through the task pid helper.
- Flow/Lifetime: OK, tasklist locking, `release_task()`, and `put_task_struct()` ordering are unchanged.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: The patch intentionally leaves direct pid comparisons in wait target matching for later narrow alignment steps.
