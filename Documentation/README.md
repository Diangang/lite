# Lite Documentation

## Purpose

This directory keeps only current, actively maintained project documentation at
the top level. Historical plans, debug notes, old audits, and superseded status
documents live under `Documentation/archived/`.

Use source code as the final authority. When documentation and code disagree,
update the documentation before using it as a planning base.

## Main Documents

- `Current-State.md`
  - Full current-code analysis.
  - Use this first to understand what Lite can do today and where the major
    Linux 2.6 gaps are.

- `Linux26-Roadmap.md`
  - Current forward plan for Linux 2.6 alignment.
  - Use this to decide the next implementation stage and scope boundaries.

- `Codex-Supervisor.md`
  - Long-running Codex supervisor design and operating instructions.
  - Use this when running Codex as restartable worker rounds instead of a
    single long-lived session.

- `Directory-Structure.md`
  - Current source tree map.
  - Use this to find subsystem ownership and key files.

- `device_driver_model.md`
  - Current device model reference.
  - Use this for `device`, `driver`, `bus`, `class`, `kobject`, `sysfs`, and
    `devtmpfs` semantics.

- `memory_layout.md`
  - Current memory layout reference.
  - Use this for physical memory, high-half mapping, direct map, vmalloc, and
    user address space layout.

## Supporting Data

- `linux-alignment-ledger/`
  - Generated or semi-generated alignment data and ledgers.
  - Keep here because it is an input to future Linux alignment work, not a
    narrative document.

## Archive Policy

Move a document to `archived/` when it is primarily:

- a historical debug session;
- a superseded roadmap;
- an old QA snapshot;
- an audit or matrix replaced by `Current-State.md` or `Linux26-Roadmap.md`;
- a mixed plan/status document that is no longer the planning authority.

Archived documents may still contain useful investigation history, but they are
not current truth unless revalidated against source.
