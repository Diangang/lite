# Review: stage2-initcall-command-line

Final commit: see `git log -1`.
Pre-review commit: e0611f04328f1317a315f4c4117903967fd41cc6

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::initcall_command_line`
- Linux reference: `linux2.6/init/main.c::setup_command_line`
- Linux reference: `linux2.6/init/main.c::do_initcall_level`
- Lite target: `init/main.c::initcall_command_line`
- Lite target: `init/main.c::setup_command_line`
- Lite target: `init/main.c::do_initcall_level`
- Single difference: Lite now keeps Linux-shaped per-initcall command-line storage and refreshes it before each initcall level.

Linux stores a separate `initcall_command_line` copy so each initcall level can
parse module parameters without mutating the saved command line. Lite does not
yet implement Linux `parse_args()` for module parameters, so this patch only
adds the Linux-named storage and refresh point needed by that later alignment.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
