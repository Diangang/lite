# Stage 4 Review: idr_alloc_cyclic API

Patch: `stage4-idr-alloc-cyclic-api`

## Linux Alignment Report

Change scope:
- Files: `lib/idr.c`, `include/linux/idr.h`
- Directories: `lib`, `include/linux`
- Public surface: `idr_alloc_cyclic()`

Reference-first evidence:
- Linux file: `linux2.6/lib/idr.c`
- Linux symbol: `idr_alloc_cyclic`
- Linux header: `linux2.6/include/linux/idr.h`
- Lite files: `lib/idr.c`, `include/linux/idr.h`
- This step only changes: add Linux's same-name cyclic allocation API as a wrapper over Lite's existing `idr_alloc()` subset.

Mapping ledger:
- Functions:
  - `idr_alloc_cyclic`: `linux2.6/lib/idr.c::idr_alloc_cyclic`, lite=`lib/idr.c`, placement=OK
- Structs:
  - `struct idr`: `linux2.6/include/linux/idr.h::struct idr`, lite=`include/linux/idr.h`, placement=OK for Lite subset
- Globals/statics: none
- Files:
  - `linux2.6/lib/idr.c`, lite=`lib/idr.c`, placement=OK
  - `linux2.6/include/linux/idr.h`, lite=`include/linux/idr.h`, placement=OK
- Directories:
  - `linux2.6/lib`, lite=`lib`, placement=OK
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, `idr_alloc_cyclic` matches Linux.
- Placement: OK.
- Semantics: OK for Lite subset, retries from `start` only after `-ENOSPC` and advances `idr->cur` on success.
- Flow/Lifetime: OK, no allocation lifetime change beyond existing `idr_alloc()`.

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
- `git show -- lib/idr.c include/linux/idr.h state.json Documentation/reviews/2026-04-30-stage4-idr-alloc-cyclic-api.md`

Findings: none.
