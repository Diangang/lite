# Stage 4 Review: idr_for_each_entry macro

Patch: `stage4-idr-for-each-entry-macro`

## Linux Alignment Report

Change scope:
- Files: `include/linux/idr.h`
- Directories: `include/linux`
- Public surface: `idr_for_each_entry`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/idr.h`
- Linux symbol: `idr_for_each_entry`
- Lite file: `include/linux/idr.h`
- This step only changes: add Linux's typed IDR iteration macro using `idr_get_next()`.

Mapping ledger:
- Functions/macros:
  - `idr_for_each_entry`: `linux2.6/include/linux/idr.h::idr_for_each_entry`, lite=`include/linux/idr.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/idr.h` -> `include/linux/idr.h`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, iterates from id zero using `idr_get_next()` and increments the found id after each entry.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: first run terminated at `NVMe MinixFS Mount + R/W` without an assertion failure; immediate rerun passed.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/idr.h state.json Documentation/reviews/2026-04-30-stage4-idr-for-each-entry-macro.md`

Findings: none.
