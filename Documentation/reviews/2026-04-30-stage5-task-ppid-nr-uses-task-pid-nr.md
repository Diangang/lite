# Stage 5 Review: task_ppid_nr uses task_pid_nr

## Linux Alignment Report

Change scope:
- files: `include/linux/sched.h`
- directories: `include/linux/`
- public surface: none; existing inline helpers only

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbols: `task_pid_nr`, `task_ppid_nr`
- Lite file: `include/linux/sched.h`
- This step only changes: `task_ppid_nr()` derives the parent id through Lite's Linux-shaped task id helper instead of directly reading `real_parent->pid`.

Mapping ledger:
- functions:
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h`, placement=OK
  - `task_ppid_nr`: `linux2.6/include/linux/sched.h::task_ppid_nr`, lite=`include/linux/sched.h`, placement=OK
- structs:
  - `task_struct`: `linux2.6/include/linux/sched.h::task_struct`, lite=`include/linux/sched.h`, placement=OK
- globals/statics: none
- files:
  - `linux2.6/include/linux/sched.h` -> `include/linux/sched.h`, placement=OK
- directories:
  - `linux2.6/include/linux/` -> `include/linux/`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK -> Lite has no pid namespaces and treats tgid as pid, so using `task_pid_nr(real_parent)` preserves current PPid values.
- Flow/Lifetime: OK

If DIFF:
- Why: Linux routes PPid through pid helpers; Lite previously read the raw parent pid field.
- Impact: No user-visible `/proc/<pid>/status` PPid value changes are intended.
- Plan: Move `task_pid_nr()` above `task_ppid_nr()` and make `task_ppid_nr()` call it for `real_parent`.

## Validation

- `make -j4` passed.
- `make smoke-128` passed.
- `make smoke-512` initially failed before the preempt-count/NVMe poll fix.
- After `stage5-preempt-count-protects-nvme-poll`, the same tree with this helper patch passed:
  - `make -j4`
  - `make smoke-128`
  - `make smoke-512`
  - `make smoke-512` again

## Review

Findings: none.
