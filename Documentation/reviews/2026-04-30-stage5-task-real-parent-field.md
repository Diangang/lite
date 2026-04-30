# Stage 5 Review: task real_parent field

Patch: `stage5-task-real-parent-field`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`, `kernel/fork.c`, `kernel/exit.c`, `kernel/sched/core.c`
- Directories: `include/linux`, `kernel`
- Public surface: `struct task_struct::real_parent`

Reference-first evidence:
- Linux files: `linux2.6/include/linux/sched.h`, `linux2.6/kernel/fork.c`, `linux2.6/kernel/exit.c`
- Linux symbols: `task_struct::real_parent`, `task_struct::parent`
- Lite files: `include/linux/sched.h`, `kernel/fork.c`, `kernel/exit.c`, `kernel/sched/core.c`
- This step only changes: add Linux's natural-parent field and keep it synchronized with Lite's existing parent/wait relationship.

Mapping ledger:
- Functions:
  - `sys_fork`: `linux2.6/kernel/fork.c::copy_process`, lite=`kernel/fork.c`, placement=OK
  - `reparent_children`: `linux2.6/kernel/exit.c::forget_original_parent`, lite=`kernel/exit.c`, placement=OK
  - `release_task`: `linux2.6/kernel/exit.c::release_task`, lite=`kernel/exit.c`, placement=OK
  - `init_task`: Linux init task initialization, lite=`kernel/sched/core.c`, placement=OK for Lite scheduler initialization
- Structs:
  - `task_struct::real_parent`: `linux2.6/include/linux/sched.h::task_struct::real_parent`, lite=`include/linux/sched.h`, placement=OK
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
  - `linux2.6/kernel/fork.c`, lite=`kernel/fork.c`, placement=OK
  - `linux2.6/kernel/exit.c`, lite=`kernel/exit.c`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
  - `linux2.6/kernel`, lite=`kernel`, placement=OK
- NO_DIRECT_LINUX_MATCH:
  - `task_create_internal`: Lite private task creation helper; it initializes the same `task_struct` parent fields because it allocates tasks outside `sys_fork`.

Consistency:
- Naming: OK, field name matches Linux.
- Placement: OK.
- Semantics: OK subset. Without ptrace or `CLONE_PARENT`, Lite sets `real_parent` and `parent` to the same task and reparents both together.
- Flow/Lifetime: OK. Creation initializes both fields, reparenting updates both fields, and release clears both fields before the final reference drop.

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
- `git show -- include/linux/sched.h kernel/fork.c kernel/exit.c kernel/sched/core.c state.json Documentation/reviews/2026-04-30-stage5-task-real-parent-field.md`

Findings: none.
