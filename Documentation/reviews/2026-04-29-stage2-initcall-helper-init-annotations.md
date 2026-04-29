# Review: stage2-initcall-helper-init-annotations

Final commit: see `git log -1`.
Pre-review commit: 7ce4b40767f5abfed57c7cc55753c4b7165650d2

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::do_pre_smp_initcalls`
- Linux reference: `linux2.6/init/main.c::do_initcall_level`
- Linux reference: `linux2.6/init/main.c::do_initcalls`
- Linux reference: `linux2.6/init/main.c::do_basic_setup`
- Lite target: `init/main.c::do_pre_smp_initcalls`
- Lite target: `init/main.c::do_initcall_level`
- Lite target: `init/main.c::do_initcalls`
- Lite target: `init/main.c::do_basic_setup`
- Single difference: Lite now marks the initcall setup helpers with Linux's `__init` annotation.

Linux treats these helpers as initialization-only code. Lite currently keeps
`__init` as a no-op because init text is not reclaimed yet, so this is a naming
and lifetime annotation alignment without runtime behavior change.

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
