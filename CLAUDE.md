# Oculus: notes (not final)

Oculus is a commercial static analyzer for C and C++. These are the decisions taken so
far. The architecture (code structure, IR, module layout, test layout) is still
to be defined; do not treat anything below as a spec.

## Decided so far

- Everything in this repository is in English: docs, comments, messages, CLI options,
  config keys and program symbols (classes, functions, variables, files).
- Implementation language: AIML v2 (Pymann/aiml `masterv2`).
- Frontend: Clang, through a C++ LibTooling worker `oculus-clang` (`worker/`), one
  process per TU, built against the newest stable LLVM. It writes the RuleInformation
  of `doc/ir.md` as JSON; nothing of Clang reaches the AIML side.
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
- The rest of the IR (`doc/ir.md`) and the rule interface details.

## Language and layout

- Written in AIML v2 (the `masterv2` line of Pymann/aiml). Follow its static rules:
  at most 3 models/classes/interfaces per file, identifiers of 3+ characters, no
  string switch on one variable (use a map or an enum), at most 5 arguments.
- `src/main.aiml` entry and CLI; `src/runtime.aiml` the `ScanRuntime` class group
  that wires the parts; `src/config` CLI and `oculus.json`; `src/scan` compilation
  database, planner, scanner; `src/frontend` the `Frontend` interface (only a stub
  so far); `src/ir` the own IR; `src/metrics` metric providers; `src/rules` the
  `Rule` interface, registry and rules; `src/report` findings and output formats.
- Imports are written relative to the repository root.

## Build and CI

- Toolchain: the aiml venv from Kontor, `generic/aiml/<commit>/aiml-x86_64-linux.tar.gz`
  (credentials in the `AIML_KONTOR_*` repo secrets). The pinned commit is
  `AIML_PINNED_REF` in `.github/workflows/ci.yml`.
- Build: `aiml build` (reads `exodus.json`, aiml spec/68 section 7) gives `out/oculus/oculus`;
  then `worker/build.sh` (needs `llvm-config` or `$LLVM_CONFIG`, LLVM and Clang dev
  packages) gives `out/oculus/oculus-clang`. `aiml build` clears `out/oculus`, so the
  worker is built after it.
- Test: `tests/run_tests.sh out/oculus/oculus` runs `tests/unit/*.aiml` (JIT, exit 0 = pass),
  the end-to-end checks against `tests/e2e/`, and `tests/run_catalog.sh`: the bad and good
  examples of each implemented rule, copied from coding-rules into `tests/catalog/` with
  `tools/sync_catalog_tests.sh`.
- Commit directly to `main`, no pull requests.
