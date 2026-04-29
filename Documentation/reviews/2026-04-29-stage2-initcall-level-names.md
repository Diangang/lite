# Review: stage2-initcall-level-names

Final commit: see `git log -1`.
Pre-review commit: 0aee4791ff1f41533975d628f1f6fa6e09811471

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::initcall_level_names`
- Linux reference: `linux2.6/init/main.c::do_initcall_level`
- Lite target: `init/main.c::initcall_level_names`
- Lite target: `init/main.c::do_initcall_level`
- Single difference: Lite now carries the Linux-shaped initcall level name table next to `initcall_levels`.

Linux keeps `initcall_level_names[]` synchronized with the initcall macros in
`include/linux/init.h` and passes the current name to per-level parameter
parsing. Lite does not yet implement that full `parse_args()` path, so this
patch adds the same metadata and uses it for the level bound check without
changing initcall execution order.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount test.

## Findings

None.
