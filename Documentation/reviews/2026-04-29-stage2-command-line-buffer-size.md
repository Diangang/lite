# Review: stage2-command-line-buffer-size

Final commit: see `git log -1`.
Pre-review commit: e99b4fb8696c95d96f3b4e8bdc83659a8b9771b4

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::saved_command_line`
- Linux reference: `linux2.6/init/main.c::static_command_line`
- Linux reference: `linux2.6/init/main.c::initcall_command_line`
- Lite target: `init/main.c::saved_command_line`
- Lite target: `init/main.c::static_command_line`
- Lite target: `init/main.c::initcall_command_line`
- Single difference: Lite now sizes its command-line backing buffers from the Linux x86 `COMMAND_LINE_SIZE` limit.

Linux allocates command-line copies large enough for the boot command line.
Lite keeps fixed backing arrays as a subset, but now derives those arrays from
`COMMAND_LINE_SIZE` instead of a smaller local 256-byte cap.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
