# Stage 5 Review: add getppid syscall dispatch

Patch: `stage5-add-getppid-syscall-dispatch`

## Linux Alignment Report

Change scope:
- Files: `arch/x86/include/asm/unistd.h`, `arch/x86/entry/syscall_32.c`
- Directories: `arch/x86`
- Public surface: `SYS_GETPPID`

Reference-first evidence:
- Linux files: `linux2.6/kernel/sys.c`, `linux2.6/include/linux/sched.h`
- Linux symbols: `sys_getppid`, `task_ppid_nr`
- Lite files: `arch/x86/include/asm/unistd.h`, `arch/x86/entry/syscall_32.c`
- This step only changes: add Lite syscall dispatch for `getppid`, returning the current task's parent PID through `task_ppid_nr()`.

Mapping ledger:
- Functions:
  - `sys_getppid`: `linux2.6/kernel/sys.c::sys_getppid`, lite dispatch=`arch/x86/entry/syscall_32.c::sys_getppid_dispatch`, placement=DIFF
  - `task_ppid_nr`: `linux2.6/include/linux/sched.h::task_ppid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics:
  - `sys_call_table`: Linux syscall table concept, lite=`arch/x86/entry/syscall_32.c`, placement=OK for existing Lite dispatch shape
- Files:
  - `linux2.6/kernel/sys.c`, lite syscall dispatch currently in `arch/x86/entry/syscall_32.c`, placement=DIFF
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/kernel`, lite syscall dispatch currently in `arch/x86/entry`, placement=DIFF
- NO_DIRECT_LINUX_MATCH: none for syscall semantics.

Consistency:
- Naming: OK, syscall name and user-visible number use Linux terminology.
- Placement: DIFF, follows Lite's existing syscall dispatch architecture.
- Semantics: OK as a Lite subset; returns parent TGID/PID.
- Flow/Lifetime: OK, read-only syscall addition.

If DIFF:
- Why: Lite currently keeps syscall dispatch in the architecture entry path instead of Linux's `kernel/sys.c`.
- Impact: adds one syscall number inside the existing `NR_syscalls` bound; no existing syscall numbers move.
- Plan: handle broader syscall implementation placement convergence separately.

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
- `git show -- arch/x86/include/asm/unistd.h arch/x86/entry/syscall_32.c state.json Documentation/reviews/2026-04-30-stage5-add-getppid-syscall-dispatch.md`

Findings: none.
