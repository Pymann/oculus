# Oculus: notes (not final)

Oculus is a commercial static analyzer for C and C++. These are the decisions taken so
far. The architecture (code structure, IR, module layout, test layout) is still
to be defined; do not treat anything below as a spec.

## Decided so far

- Everything in this repository is in English: docs, comments, messages, CLI options,
  config keys and program symbols (classes, functions, variables, files).
- Frontend: Clang. How Oculus binds to it is open (see `doc/architecture.md`).
- Scope: C and C++ only.
- Name: Oculus only (binary `oculus`, config `oculus.json`). Config format: JSON only.
- No Cppcheck and no other GPL code, not even as a reference copied from.
- Own rule set `OC-xxxx`; the id is a number that never changes. Catalogue in
  `Pymann/coding-rules`. Rules map to MISRA C / CERT C **by number only**, kept
  internal there, not shipped. Only a generic `Rule`, nothing named after MISRA or CERT.
  No MISRA or CERT rule texts in the repository; every message is our own wording.
- Output format, one finding per line: `file:line:col: RULE-ID: message`
- One worker process per translation unit.
- Own immutable IR.
- Phase 2: cross-TU facts.
- Metrics alongside the rules (lines of code, cyclomatic complexity and the like).
  Own line counting, no cloc.
- Input: `compile_commands.json`.

## Open

- Target standard: MISRA C:2012 or a newer edition, and which C++ guidelines.
- Clang binding (libclang C API or LibTooling) and implementation language.
- IR design and the rule interface. Draft: `doc/architecture.md`.

## Build and CI

- Built with exodus (`exodus build`, config in `exodus.json`). CI installs
  exodus from Kontor, not from GitHub: see `.github/workflows/ci.yml`.
- `src/main.cpp` is a placeholder so CI has something to build.
