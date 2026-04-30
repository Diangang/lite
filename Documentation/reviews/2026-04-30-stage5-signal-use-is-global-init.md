# Stage 5 Review: signal uses is_global_init

Patch: `stage5-signal-use-is-global-init`

## Linux Alignment Report

Change scope:
- Files: `kernel/signal.c`
- Directories: `kernel`
- Public surface: none

Reference-first evidence:
- Linux files: `linux2.6/include/linux/sched.h`, `linux2.6/kernel/signal.c`
- Linux symbol: `is_global_init`
- Lite file: `kernel/signal.c`
- This step only changes: use the Linux init-task predicate helper for Lite's existing PID 1 kill guard.

Mapping ledger:
- Functions:
  - `sys_kill`: Lite signal syscall implementation, lite=`kernel/signal.c`, placement=OK
  - `is_global_init`: `linux2.6/include/linux/sched.h::is_global_init`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/kernel/signal.c`, lite=`kernel/signal.c`, placement=OK
- Directories:
  - `linux2.6/kernel`, lite=`kernel`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, caller uses the Linux helper name.
- Placement: OK.
- Semantics: OK, behavior remains “do not kill global init”.
- Flow/Lifetime: OK, predicate-only caller change.

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
- `git show -- kernel/signal.c state.json Documentation/reviews/2026-04-30-stage5-signal-use-is-global-init.md`

Findings: none.
