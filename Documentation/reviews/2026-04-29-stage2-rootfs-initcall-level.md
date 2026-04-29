# Review: stage2-rootfs-initcall-level

Final commit: see `git log -1`.
Pre-review commit: 37e3b5542a584057b22889c1a28630579a69098a

## Scope

- `include/linux/init.h`
- `arch/x86/kernel/linker.ld`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/init.h::rootfs_initcall`
- Linux reference: `linux2.6/include/asm-generic/vmlinux.lds.h::INIT_CALLS`
- Lite target: `include/linux/init.h::rootfs_initcall`
- Lite target: `arch/x86/kernel/linker.ld::.initcall.init`
- Single difference: add the Linux-shaped `rootfs_initcall()` layer between fs and device initcalls.

Linux runs `rootfs_initcall()` after level 5 fs initcalls and before level 6
device initcalls. Lite now exposes the same macro and linker placement. This
does not move `populate_rootfs()` yet because Lite currently unpacks the
initramfs after its VFS root has been initialized in `prepare_namespace()`.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/init.h arch/x86/kernel/linker.ld state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed on the third exact run; first two runs hit NVMe read timeouts during `/mnt_nvme` mount or subsequent smoke.
- `make smoke-512`: passed

## Findings

None.
