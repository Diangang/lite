# Review: stage2-init-command-fallback

Final commit: see `git log -1`.
Pre-review commit: 0dd4d67302a46dcd320f72ad1094b0ded0d13c3f

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::execute_command`
- Linux reference: `linux2.6/init/main.c::kernel_init`
- Lite target: `init/main.c::execute_command`
- Lite target: `init/main.c::prepare_namespace`
- Single difference: explicit `init=` now behaves like Linux by preventing fallback when the requested init command fails.

Linux leaves `execute_command` unset unless `init=` is present. If a requested
init command fails, Linux panics instead of trying the default init fallback
list. Lite now keeps fallback only for the no-explicit-init path.

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
