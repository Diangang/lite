# Stage 4 Review: radix-tree internal lookup API

Patch: `stage4-radix-tree-internal-lookup-api`

## Linux Alignment Report

Change scope:
- Files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- Directories: `lib`, `include/linux`
- Public surface: `__radix_tree_lookup()`

Reference-first evidence:
- Linux files: `linux2.6/lib/radix-tree.c`, `linux2.6/include/linux/radix-tree.h`
- Linux symbols: `__radix_tree_lookup`, `radix_tree_lookup`, `radix_tree_lookup_slot`
- Lite files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- This step only changes: add Linux's shared lookup helper and route the existing lookup APIs through it.

Mapping ledger:
- Functions/macros:
  - `__radix_tree_lookup`: `linux2.6/lib/radix-tree.c::__radix_tree_lookup`, lite=`lib/radix-tree.c`, placement=OK
  - `radix_tree_lookup`: `linux2.6/lib/radix-tree.c::radix_tree_lookup`, lite=`lib/radix-tree.c`, placement=OK
  - `radix_tree_lookup_slot`: `linux2.6/lib/radix-tree.c::radix_tree_lookup_slot`, lite=`lib/radix-tree.c`, placement=OK
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
- Semantics: OK for Lite's existing non-RCU tree shape; returns the found item and optionally the containing node and slot, while existing lookup APIs retain their previous NULL-on-miss behavior.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/radix-tree.h lib/radix-tree.c state.json Documentation/reviews/2026-04-30-stage4-radix-tree-internal-lookup-api.md`

Findings: none.
