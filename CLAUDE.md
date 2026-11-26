# Oculus: notes (not final)

Oculus is a commercial static analyzer for C. These are the decisions taken so
far. The architecture (code structure, IR, module layout, test layout) is still
to be defined; do not treat anything below as a spec.

## Decided so far

- Frontend: Clang/LLVM.
- No Cppcheck and no other GPL code, not even as a reference copied from.
- Own rule set `OC-xxxx`. Rules map to MISRA C / CERT C **by number only**.
  No MISRA or CERT rule texts in the repository; every message is our own wording.
- Output format, one finding per line: `file:line:col: RULE-ID: message`
- One worker process per translation unit.
- Own immutable IR.
- Phase 2: cross-TU facts.
- Metrics alongside the rules (lines of code, cyclomatic complexity and the like).

## Open

- Target standard: MISRA C:2012, a newer edition, or C++ as well.
- Everything about the architecture.

## Build and CI

- Built with exodus (`exodus build`, config in `exodus.json`). CI installs
  exodus from Kontor, not from GitHub: see `.github/workflows/ci.yml`.
- `src/main.cpp` is a placeholder so CI has something to build.
