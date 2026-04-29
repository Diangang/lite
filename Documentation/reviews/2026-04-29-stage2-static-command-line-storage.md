# Review: stage2-static-command-line-storage

Final commit: see `git log -1`.
Pre-review commit: 0e09b5c51846c51e17a38b29bcfe4b781492ab55

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::static_command_line`
- Linux reference: `linux2.6/init/main.c::setup_command_line`
- Lite target: `init/main.c::static_command_line`
- Lite target: `init/main.c::setup_command_line`
- Single difference: Lite now keeps a Linux-named static command-line copy for later parameter parsing alignment.

Linux keeps `static_command_line` as the mutable command-line copy used by
parameter parsing. Lite does not yet wire in Linux `parse_args()`, so this patch
only adds the same storage role and refresh point while preserving the existing
saved-command-line capacity.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; an earlier run hit an NVMe read timeout during namespace mount.
- `make smoke-512`: passed

## Findings

None.
