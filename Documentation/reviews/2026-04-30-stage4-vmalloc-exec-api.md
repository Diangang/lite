# Stage 4 Review: vmalloc_exec API

Patch: `stage4-vmalloc-exec-api`

## Linux Alignment Report

Change scope:
- Files: `mm/vmalloc.c`, `include/linux/vmalloc.h`
- Directories: `mm`, `include/linux`
- Public surface: `vmalloc_exec()`

Reference-first evidence:
- Linux files: `linux2.6/mm/vmalloc.c`, `linux2.6/include/linux/vmalloc.h`
- Linux symbols: `vmalloc_exec`
- Lite files: `mm/vmalloc.c`, `include/linux/vmalloc.h`
- This step only changes: add Linux's executable vmalloc entry point.

Mapping ledger:
- Functions/macros:
  - `vmalloc_exec`: `linux2.6/mm/vmalloc.c::vmalloc_exec`, lite=`mm/vmalloc.c`, placement=OK
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
- Semantics: OK for Lite's current paging model, where kernel vmalloc mappings do not distinguish executable from non-executable protection.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/vmalloc.h mm/vmalloc.c state.json Documentation/reviews/2026-04-30-stage4-vmalloc-exec-api.md`

Findings: none.
