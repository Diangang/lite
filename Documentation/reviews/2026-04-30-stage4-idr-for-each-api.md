# Stage 4 Review: idr_for_each API

Patch: `stage4-idr-for-each-api`

## Linux Alignment Report

Change scope:
- Files: `include/linux/idr.h`, `lib/idr.c`
- Directories: `include/linux`, `lib`
- Public surface: `idr_for_each()`

Reference-first evidence:
- Linux files: `linux2.6/include/linux/idr.h`, `linux2.6/lib/idr.c`
- Linux symbol: `idr_for_each`
- Lite files: `include/linux/idr.h`, `lib/idr.c`
- This step only changes: add Linux's callback-based IDR iteration API.

Mapping ledger:
- Functions:
  - `idr_for_each`: `linux2.6/lib/idr.c::idr_for_each`, lite=`lib/idr.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/idr.h` -> `include/linux/idr.h`, placement=OK
  - `linux2.6/lib/idr.c` -> `lib/idr.c`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, `linux2.6/lib` -> `lib`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, calls the callback for each registered pointer, returns the first non-zero callback result, and avoids signed overflow if the final visited id is `INT_MAX`.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/idr.h lib/idr.c state.json Documentation/reviews/2026-04-30-stage4-idr-for-each-api.md`

Findings: none.
