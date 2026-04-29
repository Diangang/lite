# Review: stage2-no-init-panic-message

Final commit: see `git log -1`.
Pre-review commit: ae533ae69cae9d56ff6894819377f8ca4f945c8c

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::kernel_init`
- Lite target: `init/main.c::prepare_namespace`
- Single difference: Lite now uses Linux's no-working-init panic guidance text.

Linux reports "No working init found" and points to `Documentation/init.txt`
after all init fallback attempts fail. Lite now uses the same guidance in its
panic path. Normal smoke boots do not reach this path.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe raw test.

## Findings

None.
