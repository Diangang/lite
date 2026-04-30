# Stage 4 Review: radix-tree path and count constants

Patch: `stage4-radix-tree-path-count-constants`

## Linux Alignment Report

Change scope:
- Files: `include/linux/radix-tree.h`
- Directories: `include/linux`
- Public surface: `RADIX_TREE_HEIGHT_SHIFT`, `RADIX_TREE_HEIGHT_MASK`, `RADIX_TREE_COUNT_SHIFT`, `RADIX_TREE_COUNT_MASK`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/radix-tree.h`
- Linux symbols: `RADIX_TREE_HEIGHT_SHIFT`, `RADIX_TREE_HEIGHT_MASK`, `RADIX_TREE_COUNT_SHIFT`, `RADIX_TREE_COUNT_MASK`
- Lite file: `include/linux/radix-tree.h`
- This step only changes: add Linux's radix-tree path height and count bit constants.

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
- Semantics: OK, formulas match Linux using Lite's existing map shift and max path constants.
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
- `git show -- include/linux/radix-tree.h state.json Documentation/reviews/2026-04-30-stage4-radix-tree-path-count-constants.md`

Findings: none.
