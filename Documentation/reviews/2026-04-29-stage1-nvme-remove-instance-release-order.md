# Review: Stage 1 NVMe Remove Instance Release Order

Commit under review: `7a294d4cdf0c488c5277a8eb587ce6f646537e5a`

## Scope

- Fix `nvme_remove()` teardown order in `drivers/nvme/host/pci.c`.
- Update long-run state tracking.

## Linux Reference

- `linux2.6/drivers/nvme/host/pci.c`

Linux releases the controller instance while the `nvme_dev` object is still
valid through `nvme_free_dev()`. Lite kept instance release separate, so the
remove path must release the instance before freeing the controller object.

## Evidence

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed
- `git show --check HEAD`: clean

## Findings

No code findings.

Process finding fixed by amend:

- `state.json` needed to move from validated/pending review to review-clean
  and carry the next patch candidate.

## Result

Review status: clean after amend.
