# Stage 4 Review: kstrdup Signature and Placement

Patch: `stage4-mm-kstrdup-signature-placement`

## Linux Alignment Report

Change scope:
- Files: `mm/util.c`, `include/linux/string.h`, `lib/string.c`, `fs/dcache.c`, `fs/namespace.c`
- Directories: `mm`, `include/linux`, `lib`, `fs`
- Public surface: `kstrdup(const char *s, gfp_t gfp)`

Reference-first evidence:
- Linux files: `linux2.6/mm/util.c`, `linux2.6/include/linux/string.h`
- Linux symbol: `kstrdup`
- Lite files: `mm/util.c`, `include/linux/string.h`, `lib/string.c`
- This step only changes: align `kstrdup()` to Linux's gfp-taking signature and `mm/util.c` implementation placement.

Mapping ledger:
- Functions/macros:
  - `kstrdup`: `linux2.6/mm/util.c::kstrdup`, lite=`mm/util.c`, placement=OK
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
- Semantics: OK, preserves NULL handling and full string copy; `gfp_t` is accepted as a no-op until Lite's `kmalloc` gains Linux's gfp parameter.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- fs/dcache.c fs/namespace.c include/linux/string.h lib/string.c mm/util.c state.json Documentation/reviews/2026-04-30-stage4-mm-kstrdup-signature-placement.md`

Findings: none.
