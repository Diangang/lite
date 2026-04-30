# Stage 3 Review: wait bit key initializer

Patch: `stage3-wait-bit-key-initializer`

## Linux Alignment Report

Change scope:
- Files: `include/linux/wait.h`
- Directories: `include/linux`
- Public surface: `__WAIT_BIT_KEY_INITIALIZER(word, bit)`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/wait.h`
- Linux symbol: `__WAIT_BIT_KEY_INITIALIZER`
- Lite file: `include/linux/wait.h`
- This step only changes: add the Linux wait-bit key initializer macro.

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
- Placement: OK, macro is in `include/linux/wait.h` next to wait queue initializers.
- Semantics: OK, initializer sets `.flags` and `.bit_nr` like Linux.
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
- `git show -- include/linux/wait.h state.json Documentation/reviews/2026-04-30-stage3-wait-bit-key-initializer.md`

Findings: none.
