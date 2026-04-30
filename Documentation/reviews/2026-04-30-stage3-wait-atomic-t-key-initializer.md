# Stage 3 Review: atomic wait bit key initializer

Patch: `stage3-wait-atomic-t-key-initializer`

## Linux Alignment Report

Change scope:
- Files: `include/linux/wait.h`
- Directories: `include/linux`
- Public surface: `__WAIT_ATOMIC_T_KEY_INITIALIZER(p)`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/wait.h`
- Linux symbol: `__WAIT_ATOMIC_T_KEY_INITIALIZER`
- Lite file: `include/linux/wait.h`
- This step only changes: add the Linux atomic wait-bit key initializer macro.

Mapping ledger:
- Functions: none
- Structs:
  - `wait_bit_key`: `linux2.6/include/linux/wait.h::wait_bit_key`, lite=`include/linux/wait.h`, placement=OK
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/wait.h`, lite=`include/linux/wait.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, macro name matches Linux.
- Placement: OK, macro is in `include/linux/wait.h` next to wait-bit initializers.
- Semantics: OK, initializer sets `.flags` and `WAIT_ATOMIC_T_BIT_NR` like Linux.
- Flow/Lifetime: OK, this is a declaration-time initializer only.

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
- `git show -- include/linux/wait.h state.json Documentation/reviews/2026-04-30-stage3-wait-atomic-t-key-initializer.md`

Findings: none.
