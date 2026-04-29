# Review: stage2-x86-command-line-size

Final commit: see `git log -1`.
Pre-review commit: 55e43d4b878d88a07f7e668db1db64b9a8af2c0f

## Scope

- `arch/x86/include/asm/setup.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/arch/x86/include/asm/setup.h::COMMAND_LINE_SIZE`
- Lite target: `arch/x86/include/asm/setup.h::COMMAND_LINE_SIZE`
- Single difference: Lite now defines the Linux x86 command-line size constant.

Linux exposes `COMMAND_LINE_SIZE` from the x86 setup header and uses it for
boot command-line storage in `init/main.c`. This patch only adds the matching
constant; it does not yet change Lite's fixed command-line buffers or parsing
behavior.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- arch/x86/include/asm/setup.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount test.
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe raw/minix tests.

## Findings

None.
