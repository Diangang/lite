#ifndef ASM_PTRACE_H
#define ASM_PTRACE_H

#include <stdint.h>

struct pt_regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

/*
 * Linux mapping: x86 32-bit interrupt/trap register frame (pt_regs).
 *
 * This layout matches `arch/x86/entry/entry_32.S` stubs:
 * - common stub does: `pusha`, then pushes saved `ds`, then passes `%esp` to C.
 * - the entry stubs ensure there are always 2 words below the GPRs:
 *   - `int_no` (interrupt vector number)
 *   - `err_code` (0 for IRQs and exceptions without a CPU error code)
 *
 * For CPU exceptions with an error code, the CPU pushes `err_code` and the
 * stub only pushes `int_no`. For exceptions without an error code and for IRQs,
 * the stub pushes a dummy 0 `err_code` and then `int_no`.
 *
 * Notes:
 * - `useresp`/`ss` are only valid when the trap happened from CPL3 (user mode).
 * - syscall entry uses an int0x80 trap gate and does not force `cli`; IRQ/ISRs
 *   are installed as interrupt gates so IF is cleared on entry.
 */

#endif
