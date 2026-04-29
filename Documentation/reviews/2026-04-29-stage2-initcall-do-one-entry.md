# Review: stage2-initcall-do-one-entry

Final commit: see `git log -1`.
Pre-review commit: cc2feb143f568094da32df0ddeeecec7d4135422

## Scope

- `init/main.c`
- `include/linux/init.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::do_one_initcall`
- Linux reference: `linux2.6/init/main.c::do_pre_smp_initcalls`
- Linux reference: `linux2.6/init/main.c::do_initcall_level`
- Linux reference: `linux2.6/include/linux/init.h::do_one_initcall`
- Lite target: `init/main.c::do_one_initcall`
- Lite target: `init/main.c::do_pre_smp_initcalls`
- Lite target: `init/main.c::do_initcall_level`
- Lite target: `include/linux/init.h::do_one_initcall`
- Single difference: Lite initcall execution now goes through the Linux-shaped `do_one_initcall()` entry point instead of calling initcall function pointers directly.

Linux centralizes initcall invocation through `do_one_initcall()` so return
handling, debug, and later initcall policy have one boundary. Lite keeps the
body minimal for now, but the call-flow and public symbol match the Linux
placement.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed on exact rerun; first run hit an NVMe read timeout while mounting `/mnt_nvme` before userspace smoke.

## Findings

None.
