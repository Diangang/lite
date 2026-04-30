# Stage 4 Review: vmalloc size type

Patch: `stage4-vmalloc-size-type`

## Linux Alignment Report

Change scope:
- Files: `include/linux/vmalloc.h`, `mm/vmalloc.c`
- Directories: `include/linux`, `mm`
- Public surface: `vmalloc()`

Reference-first evidence:
- Linux files: `linux2.6/include/linux/vmalloc.h`, `linux2.6/mm/vmalloc.c`
- Linux symbol: `vmalloc`
- Lite files: `include/linux/vmalloc.h`, `mm/vmalloc.c`
- This step only changes: align the `vmalloc()` size parameter from `uint32_t` to Linux's `unsigned long`.

Mapping ledger:
- Functions:
  - `vmalloc`: `linux2.6/mm/vmalloc.c::vmalloc`, lite=`mm/vmalloc.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/vmalloc.h` -> `include/linux/vmalloc.h`, placement=OK
  - `linux2.6/mm/vmalloc.c` -> `mm/vmalloc.c`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, `linux2.6/mm` -> `mm`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, public prototype now accepts Linux's `unsigned long` allocation size type.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/vmalloc.h mm/vmalloc.c state.json Documentation/reviews/2026-04-30-stage4-vmalloc-size-type.md`

Findings: none.
