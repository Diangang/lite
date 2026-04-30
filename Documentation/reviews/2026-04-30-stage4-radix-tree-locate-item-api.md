# Stage 4 Review: radix_tree_locate_item API

Patch: `stage4-radix-tree-locate-item-api`

## Linux Alignment Report

Change scope:
- Files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- Directories: `lib`, `include/linux`
- Public surface: `radix_tree_locate_item()`

Reference-first evidence:
- Linux files: `linux2.6/lib/radix-tree.c`, `linux2.6/include/linux/radix-tree.h`
- Linux symbols: `radix_tree_locate_item`
- Lite files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- This step only changes: add Linux's item-to-index lookup helper for Lite's current radix-tree layout.

Mapping ledger:
- Functions/macros:
  - `radix_tree_locate_item`: `linux2.6/lib/radix-tree.c::radix_tree_locate_item`, lite=`lib/radix-tree.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/lib/radix-tree.c` -> `lib/radix-tree.c`, placement=OK
  - `linux2.6/include/linux/radix-tree.h` -> `include/linux/radix-tree.h`, placement=OK
- Directories: `linux2.6/lib` -> `lib`, `linux2.6/include/linux` -> `include/linux`
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK for Lite's non-RCU tree shape, returning the found index or `(unsigned long)-1` when absent.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/radix-tree.h lib/radix-tree.c state.json Documentation/reviews/2026-04-30-stage4-radix-tree-locate-item-api.md`

Findings: none.
