# Stage 4 Review: idr_get_next API

Patch: `stage4-idr-get-next-api`

## Linux Alignment Report

Change scope:
- Files: `include/linux/idr.h`, `lib/idr.c`
- Directories: `include/linux`, `lib`
- Public surface: `idr_get_next()`

Reference-first evidence:
- Linux files: `linux2.6/include/linux/idr.h`, `linux2.6/lib/idr.c`
- Linux symbol: `idr_get_next`
- Lite files: `include/linux/idr.h`, `lib/idr.c`
- This step only changes: add Linux's next-entry lookup API using Lite's existing radix-tree-backed IDR storage.

Mapping ledger:
- Functions:
  - `idr_get_next`: `linux2.6/lib/idr.c::idr_get_next`, lite=`lib/idr.c`, placement=OK
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
- Semantics: OK, returns the next registered object at or after `*nextidp`, updates `*nextidp` to the found id, and skips empty radix subtrees instead of linearly scanning the full id range.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/idr.h lib/idr.c state.json Documentation/reviews/2026-04-30-stage4-idr-get-next-api.md`

Findings: none.
