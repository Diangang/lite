# Review: stage2-boot-command-line-storage

Final commit: see `git log -1`.
Pre-review commit: 550c23840500704068aa9b35aaa729b904809f22

## Scope

- `init/main.c`
- `include/linux/init.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::boot_command_line`
- Linux reference: `linux2.6/init/main.c::setup_command_line`
- Linux reference: `linux2.6/include/linux/init.h::boot_command_line`
- Lite target: `init/main.c::boot_command_line`
- Lite target: `init/main.c::setup_command_line`
- Lite target: `include/linux/init.h::boot_command_line`
- Single difference: Lite now keeps the Linux-named untouched boot command-line buffer before filling the existing saved command-line buffer.

Linux keeps `boot_command_line` as the untouched arch-provided command line and
copies from it into later parsing buffers. Lite now adds the same symbol and
declaration, while preserving the existing smaller saved-command-line backing
buffer as a current subset.

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
