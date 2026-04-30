# Stage 4 Review: radix_tree_gang_lookup_slot API

Patch: `stage4-radix-tree-gang-lookup-slot-api`

## Linux Alignment Report

Change scope:
- Files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- Directories: `lib`, `include/linux`
- Public surface: `radix_tree_gang_lookup_slot()`

Reference-first evidence:
- Linux files: `linux2.6/lib/radix-tree.c`, `linux2.6/include/linux/radix-tree.h`
- Linux symbol: `radix_tree_gang_lookup_slot`
- Lite files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- This step only changes: add Linux's index-ascending multi-slot radix tree lookup helper.

Mapping ledger:
- Functions/macros:
  - `radix_tree_gang_lookup_slot`: `linux2.6/lib/radix-tree.c::radix_tree_gang_lookup_slot`, lite=`lib/radix-tree.c`, placement=OK
- Structs: none
- Globals/statics:
  - `radix_tree_gang_lookup_slot_node`: private Lite traversal helper for the Linux public API
- Files:
  - `linux2.6/lib/radix-tree.c` -> `lib/radix-tree.c`, placement=OK
  - `linux2.6/include/linux/radix-tree.h` -> `include/linux/radix-tree.h`, placement=OK
- Directories:
  - `linux2.6/lib` -> `lib`, placement=OK
  - `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, fills up to `max_items` present entry slots in ascending index order starting at `first_index`, and fills optional `indices`.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/radix-tree.h lib/radix-tree.c state.json Documentation/reviews/2026-04-30-stage4-radix-tree-gang-lookup-slot-api.md`

Findings: none.
