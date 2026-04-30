# Stage 4 Review: vmalloc_32 API

Patch: `stage4-vmalloc-32-api`

## Linux Alignment Report

Change scope:
- Files: `mm/vmalloc.c`, `include/linux/vmalloc.h`
- Directories: `mm`, `include/linux`
- Public surface: `vmalloc_32()`

Reference-first evidence:
- Linux files: `linux2.6/mm/vmalloc.c`, `linux2.6/include/linux/vmalloc.h`
- Linux symbols: `vmalloc_32`
- Lite files: `mm/vmalloc.c`, `include/linux/vmalloc.h`
- This step only changes: add Linux's 32-bit-addressable vmalloc entry point.

Mapping ledger:
- Functions/macros:
  - `vmalloc_32`: `linux2.6/mm/vmalloc.c::vmalloc_32`, lite=`mm/vmalloc.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/mm/vmalloc.c` -> `mm/vmalloc.c`, placement=OK
  - `linux2.6/include/linux/vmalloc.h` -> `include/linux/vmalloc.h`, placement=OK
- Directories: `linux2.6/mm` -> `mm`, `linux2.6/include/linux` -> `include/linux`
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK for Lite's current i386 allocator, where vmalloc pages are already 32-bit addressable.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: initial run was externally terminated near NVMe MinixFS testing; immediate rerun passed.
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/vmalloc.h mm/vmalloc.c state.json Documentation/reviews/2026-04-30-stage4-vmalloc-32-api.md`

Findings: none.
