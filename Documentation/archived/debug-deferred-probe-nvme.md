# Debug Session: deferred-probe-nvme [OPEN]

## Symptom
- `make -j4` passes.
- `make smoke-128` fails at `FAIL: open /mnt_nvme/nvme_rw.txt`.
- Regression window: after converting Lite deferred probe bookkeeping in `drivers/base/dd.c` from array model to Linux-style pending/active lists.

## Scope
- Lite files:
  - `drivers/base/dd.c`
  - `drivers/base/core.c`
  - `drivers/base/driver.c`
  - `include/linux/device.h`
  - `include/linux/list.h`
- Linux reference:
  - `linux2.6/drivers/base/dd.c`
  - `linux2.6/drivers/base/core.c`

## Hypotheses
1. `driver_deferred_probe_trigger()` replays `device_attach()` at a time that causes duplicate probe/bind on NVMe-related devices.
2. Deferred list membership and `get_device()/put_device()` are imbalanced, leaving a stale or prematurely released device around reprobe time.
3. Lite aligned the list containers but not the Linux trigger semantics, so dependent devices are retried too early or too late.
4. `driver_register()` now triggers deferred reprobe globally in a way the previous Lite flow did not, perturbing startup ordering.
5. The regression is not in `list_splice_tail_init()` itself, but in how Lite consumes the active list synchronously inside `driver_deferred_probe_trigger()`.

## Evidence Plan
- Add instrumentation only.
- Capture:
  - every deferred add/remove/trigger
  - every `driver_probe_device()` enter/exit and return code
  - every `device_attach()` entry for NVMe / block / filesystem-facing devices
- Reproduce with `make smoke-128`.
- Decide which hypothesis survives, then apply the smallest logic fix.

## Status
- Step 1 complete: session created and hypotheses recorded.
- Step 2 complete: instrumentation added in:
  - `drivers/base/dd.c`
  - `drivers/base/core.c`
  - `fs/file.c`
  - `fs/minix/namei.c`
- Step 3 complete: rebuilt with `make -j4` and re-ran `make smoke-128`.
- Current result: `make smoke-128` passes; `NVMe MinixFS Mount + R/W` no longer reproduces the failure.

## Evidence
- Deferred-probe evidence:
  - `deferred_probe_pending_list` stayed empty.
  - `deferred_probe_active_list` stayed empty.
  - No NVMe deferred add/remove/retry sequence was observed.
- Probe/attach evidence:
  - NVMe controllers `pci00:04.0` and `pci00:05.0` each probed once and bound successfully to `nvme`.
  - No late duplicate NVMe reprobe/bind event appeared before or during `Test 35B`.
- VFS/minix evidence:
  - `/mnt_nvme` open succeeded.
  - `/mnt_nvme/nvme_rw.txt` lookup/open succeeded for create and readback.
  - The failing symptom from the previous run did not recur.

## Hypothesis status
1. Duplicate deferred retry on NVMe path: not supported by current evidence.
2. Deferred refcount/list imbalance affecting NVMe reopen: not supported by current evidence.
3. Missing Linux-style trigger semantics breaking dependency replay: not supported in this reproduction.
4. `driver_register()` trigger perturbing startup order: not supported in this reproduction.
5. Synchronous active-list consumption corrupting later NVMe filesystem access: not supported in this reproduction.

## Interim conclusion
- The previously reported regression is currently not reproducible after rebuilding.
- The most likely explanations are:
  - an earlier stale binary/object state, or
  - a non-deterministic issue not hit in the latest reproduction.
- No business-logic fix has been applied yet; only instrumentation was added.
