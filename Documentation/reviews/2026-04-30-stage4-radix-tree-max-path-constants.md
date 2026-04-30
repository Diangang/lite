# Stage 4 Review: radix-tree max path constants

Patch: `stage4-radix-tree-max-path-constants`

## Linux Alignment Report

Change scope:
- Files: `include/linux/radix-tree.h`
- Directories: `include/linux`
- Public surface: `RADIX_TREE_INDEX_BITS`, `RADIX_TREE_MAX_PATH`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/radix-tree.h`
- Linux symbols: `RADIX_TREE_INDEX_BITS`, `RADIX_TREE_MAX_PATH`
- Lite file: `include/linux/radix-tree.h`
- This step only changes: add Linux's radix-tree index width and max path constants.

Mapping ledger:
- Functions: none
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/radix-tree.h`, lite=`include/linux/radix-tree.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, macro names match Linux.
- Placement: OK.
- Semantics: OK, constants match Linux's formula using Lite's fixed `RADIX_TREE_MAP_SHIFT`.
- Flow/Lifetime: OK, header constants only.

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
- `git show -- include/linux/radix-tree.h state.json Documentation/reviews/2026-04-30-stage4-radix-tree-max-path-constants.md`

Findings: none.
