# Review: stage2-initcall-levels-initdata

Final commit: see `git log -1`.
Pre-review commit: 32cebe55d4e5a0497ce123a1ae66137569049f0e

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::initcall_levels`
- Lite target: `init/main.c::initcall_levels`
- Single difference: Lite now marks `initcall_levels` with the Linux `__initdata` lifetime annotation.

Linux declares the initcall level pointer table as init-only data. Lite does not
free init sections yet because `__initdata` is currently a no-op, so this patch
aligns the symbol annotation without changing runtime behavior.

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
