# Stage 4 Review: kstrndup API

Patch: `stage4-mm-kstrndup-api`

## Linux Alignment Report

Change scope:
- Files: `mm/util.c`, `include/linux/string.h`
- Directories: `mm`, `include/linux`
- Public surface: `kstrndup()`

Reference-first evidence:
- Linux files: `linux2.6/mm/util.c`, `linux2.6/include/linux/string.h`
- Linux symbol: `kstrndup`
- Lite files: `mm/util.c`, `include/linux/string.h`
- This step only changes: add Linux's bounded string duplication helper API.

Mapping ledger:
- Functions/macros:
  - `kstrndup`: `linux2.6/mm/util.c::kstrndup`, lite=`mm/util.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/mm/util.c` -> `mm/util.c`, placement=OK
  - `linux2.6/include/linux/string.h` -> `include/linux/string.h`, placement=OK
- Directories:
  - `linux2.6/mm` -> `mm`, placement=OK
  - `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, duplicates at most `max` bytes and NUL-terminates the new string; `gfp_t` is accepted as a no-op until Lite's `kmalloc` gains Linux's gfp parameter.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/string.h mm/util.c state.json Documentation/reviews/2026-04-30-stage4-mm-kstrndup-api.md`

Findings: none.
