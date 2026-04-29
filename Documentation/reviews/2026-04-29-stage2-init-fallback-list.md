# Review: stage2-init-fallback-list

Final commit: see `git log -1`.
Pre-review commit: 9b453ba6987066e4713440bfeee8eeec66314c53

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::kernel_init`
- Lite target: `init/main.c::prepare_namespace`
- Single difference: Lite now uses Linux's regular init fallback list without the extra `/sbin/sh` entry.

Linux tries `/sbin/init`, `/etc/init`, `/bin/init`, then `/bin/sh`. Lite had an
extra `/sbin/sh` fallback between `/bin/init` and `/bin/sh`; this patch removes
that non-Linux fallback. The default smoke path still uses `/sbin/init`.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount test.
- `make smoke-512`: passed after rerun; an earlier run timed out after an NVMe read timeout.

## Findings

None.
