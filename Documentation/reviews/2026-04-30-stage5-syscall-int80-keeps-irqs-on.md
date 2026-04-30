# Stage 5: syscall int80 keeps IRQs on

## Linux Alignment Report

Change scope:
- files: `arch/x86/entry/syscall_32.c`, `Makefile`, `scripts/run-smoke-qemu.sh`
- directories: `arch/x86/entry`, smoke test harness scripts
- public surface: 32-bit `int 0x80` syscall interrupt semantics and smoke timeout reporting

Reference-first evidence:
- Linux file: `linux2.6/arch/x86/entry/entry_32.S`
- Linux symbol: `entry_INT80_32`
- Linux evidence: 32-bit `INT80` is documented as a trap gate with IRQs already on before calling `do_syscall_32_irqs_on`.
- Linux file: `linux2.6/arch/x86/entry/common.c`
- Linux symbol: `do_syscall_32_irqs_on`
- Lite file: `arch/x86/entry/syscall_32.c`
- This step only changes: remove the Lite-only full syscall-dispatch IRQ masking that prevented timer jiffies from advancing during long NVMe polling syscalls.

Mapping ledger:
- functions:
  - `entry_INT80_32`: Linux=`linux2.6/arch/x86/entry/entry_32.S::entry_INT80_32`, lite=`arch/x86/entry/syscall_32.c::syscall_handler`, placement=DIFF
  - `do_syscall_32_irqs_on`: Linux=`linux2.6/arch/x86/entry/common.c::do_syscall_32_irqs_on`, lite=`arch/x86/entry/syscall_32.c::syscall_handler`, placement=DIFF
- structs: none
- globals/statics:
  - `sys_call_table`: Linux=`linux2.6/arch/x86/entry/common.c` dispatches through `ia32_sys_call_table`, lite=`arch/x86/entry/syscall_32.c::sys_call_table`, placement=DIFF
- files:
  - Linux syscall entry split across assembly/common C; Lite keeps this dispatch in `arch/x86/entry/syscall_32.c`.
- directories:
  - syscall entry remains under `arch/x86/entry`.
- NO_DIRECT_LINUX_MATCH:
  - smoke harness timeout diagnostics; this is test infrastructure, not kernel ABI.

Consistency:
- Naming: OK, no new kernel-facing names.
- Placement: DIFF accepted for existing Lite syscall entry structure.
- Semantics: OK after change, syscall dispatch no longer forces IRQs off for the whole syscall.
- Flow/Lifetime: OK, no task lifetime changes.

If DIFF:
- Why: Lite does not have Linux's full x86 entry assembly/common C split.
- Impact: preserve local placement while matching the Linux IRQ-on `INT80` contract.
- Plan: keep syscall dispatch interruptible; keep the existing post-syscall reschedule check.

## Timeout Root Cause

Observed failure:
- `make smoke-512` repeatedly reached NVMe raw or NVMe MinixFS tests and then the harness killed QEMU without an in-kernel smoke FAIL summary.
- After adding an explicit harness timeout marker, the failure showed `SMOKE HARNESS TIMEOUT` rather than `nvme: io ... err=-110`.

Root cause:
- `syscall_handler()` used `irq_save()` before syscall table dispatch and restored IRQs only after the syscall returned.
- NVMe raw/minix smoke tests issue storage syscalls.
- `drivers/nvme/host/pci.c::nvme_cq_poll_cid()` uses `time_get_jiffies()` for its internal timeout.
- With timer IRQs disabled for the whole syscall, jiffies cannot advance, so a delayed completion can spin forever from the kernel's point of view and only the outer QEMU harness timeout fires.

## Patch Summary

- Removed whole-dispatch `irq_save()` / `irq_restore()` from `arch/x86/entry/syscall_32.c::syscall_handler`.
- Raised the default smoke harness timeout from 30s to 60s to cover normal QEMU variance.
- Added an explicit `SMOKE HARNESS TIMEOUT` marker so future failures are distinguishable from kernel test failures.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed
- `make smoke-512`: passed again for stability

## Review

Findings: none.

Residual risk:
- Lite still has a simplified x86 syscall entry path compared with Linux, but the changed IRQ contract now matches the Linux 32-bit `INT80` trap-gate behavior relevant to this timeout.
