# Review: Stage 1 Driver-Core TRAEDBG Cleanup

Commit under review: `dc303fd8f8d6631028b48022755a5d4eaac5c274`

## Scope

- Remove stale `deferred-probe-nvme` `TRAEDBG` instrumentation from:
  - `drivers/base/dd.c`
  - `drivers/base/core.c`
- Update current-state and long-run state tracking.

## Linux Reference

- `linux2.6/drivers/base/dd.c`

Linux uses driver-core probe/deferred-probe flow without unconditional
JSON-style `printf` tracing. Diagnostic messages in the reference are debug
logging, not always-on smoke-test output.

## Evidence

- `make -j4`: passed
- `make smoke-128`: passed after exact rerun
- `make smoke-512`: passed
- `git show --check HEAD`: clean
- `rg -n "TRAEDBG|debug-point" .`: no active code hits remain

## Findings

No code findings.

Process finding fixed by amend:

- `state.json` needed to move from validated/pending review to review-clean
  and carry the next patch candidate.

## Result

Review status: clean after amend.
