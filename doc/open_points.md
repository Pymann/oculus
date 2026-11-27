# Open points

Kept up to date with every rules batch. Each entry says what is open and what it
waits for. Rule numbers are the catalogue ids (Pymann/coding-rules).

## Decisions needed

- **OC-0022 terminated_escape, strict reading.** An octal or hex escape must be
  followed by the end of the literal or by another escape, as the catalogue text
  says. That also reports unambiguous cases such as `"One\0Two"` (imgui combo
  lists) and `"\x1b[0m"` (fmt colours). Those make up most of its findings in the
  real projects. Option: no report when the next character cannot continue the
  escape (not a hex digit after `\x`, not an octal digit after an octal escape).
  This needs a catalogue change.
- **OC-0343 no_unary_plus.** Reports every built-in unary `+`, including the
  deliberate `+1.0f` style in imgui (84 findings). It is correct by the rule text.
  Open: should written literals (`+1.0f`, `+0`) be exempt?

- **OC-0315 macros_at_global_scope.** Every #define or #undef inside a namespace
  counts, including the configuration macros fmt and gtest define between
  their namespace macros (239 findings in fmt, 206 of them in gtest). It is correct
  by the rule text. Open: should headers that only wrap a namespace be treated
  differently?

## Left out on purpose

- **OC-0386** catch_all_handler_last and **OC-0329**
  no_mixed_prefix_concatenation: Clang rejects such code. A unit that does not
  compile is reported as such and is not checked. (0386 is listed in
  catalog/not_implemented.txt; 0329 is not listed yet.)
- **OC-0189** (_Atomic void): only a compile-error example. Same reason.
- **OC-0142** include_form: an `#include` of anything but `<name>` or `"name"`
  does not compile either.

## Deferred: needs more frontend or IR

- **OC-0316** function-like macros: needs expansion records for the user's own
  macros, not only system macros. Without them a macro used as a constant
  expression cannot be told apart.
- **OC-0094** and **OC-0096**: side effects of calls in initializer lists and
  next to `++`/`--`. These are ambiguous without knowing which functions are pure
  (library model, phase 2).
- **OC-0196**: qualifiers on function types. Clang drops the qualifier before the
  AST, so the IR never sees it.
- **OC-0141** (characters in header names), **OC-0322** (C library headers in
  C++) and **OC-0375** (`std::vector<bool>`): deferred in earlier batches. The
  detailed reasons still need writing up here.

## Scope limits of implemented rules (until phase 2, cross-TU facts)

- **OC-0016** unused parameters: only functions with internal linkage whose
  address is not taken in the unit. Others may be callbacks defined elsewhere.
- **OC-0057** implicit enum values: reported only when an *earlier* enumerator
  has the value. A later explicit alias (`LAST = C`) counts as intended.
- **OC-0063** double initialisation: only lists whose elements are all designated
  and have no ranges. Lists that mix positional and designated elements are not
  checked yet.
- **OC-0286** explicit base construction: classes with dependent bases (templates)
  are not checked, and a base with no data members (an interface that has only a
  vtable) does not count.
- **OC-0369** constant assert: only arguments made of literals, `true`, `false`,
  `!`, `&&`, `||` and parentheses. `assert(sizeof(int) == 4)` is not seen. An
  assert written in a macro body is reported once, at the definition.

## Infrastructure

- **Crash isolation** for the in-process Clang frontend
  (doc/inprocess_frontend.md section 4) is not built yet. A Clang crash on unusual
  code ends the whole scan. Workaround: `--frontend json`.

## TODO: build and test pipeline in AIML (no Python, no shell scripts)

Goal: the CI workflow installs no Python, and every script of the repository is
AIML, built and run with `aiml build` (the AIML exodus, aiml spec/68 section 7).
Nothing below is done yet. The feasibility notes come from reading aiml spec/68,
spec/23 and `src/core/build.cpp` (aiml 9658e82) and have not been tried.

What the AIML side offers for this:
- Processes: `sys.exe(argv, {cwd, env, capture, stdout, stderr, stdin, timeout})`
  with `pid`, `returncode`, `running()`, `wait()`, captured output and
  `sys.wait_any` (spec/23 section 5). There is no shell unless `shell: true`.
- Files: `io.read_file`, `io.write`, `io.list_dir`, `io.glob`, `io.mkdir_all`,
  `io.copy`, `io.remove_all`, `io.rename`, `io.is_file`/`is_dir`, paths
  (spec/23, spec/42, spec/42_1). There is also regex (spec/09_2), JSON
  (`sys.serialize`/`deserialize`), `time.mono_ms` and `sys.env`.
- Build: `aiml build -c A.json -c B.json ...` builds several configurations in
  order. It supports `output_type: shared_lib`, C/C++ sources with
  `compiler.flags`, and `linker.flags`, `libraries` and `library_paths`.
  `pre_compilation`, `pre_linkage` and `post_linkage` hooks are `.aiml` files run
  by the JIT.
- Limits of `aiml build`: a hook may not change the configuration (it works on
  files only); there is no shell and there are no placeholders besides `$AIML`,
  `$AIML_VENV` and `__exodus_cache` (F68-85/86/87); there are no dependencies
  between local projects (the order is the order of `-c`).
- spec/62 (pipelines: matrix, reuse, parallel steps) would fit the CI and
  real-project runs, but it is a proposal and not implemented.

Script by script:

| Script | What it does | Feasible in AIML | Missing or to check |
| --- | --- | --- | --- |
| `worker/build.sh` | Builds `liboculus_clang.so` (LLVM static, hidden symbols, version script) and `oculus-clang` from llvm-config flags | Yes. Two exodus files, `exodus.shim.json` (shared_lib) and `exodus.worker.json` (executable), plus a `pre_compilation` hook in AIML that runs `llvm-config` and writes `out/gen/llvm_cxx.rsp` and `out/gen/llvm_link.rsp` (flags, the `libclang*.a` list in a start/end group, system libs). The JSON files name them as `@out/gen/....rsp`, which clang and g++ read themselves. The fixed flags (`-fvisibility=hidden`, `--version-script`, `-Bsymbolic`, `--gc-sections`, `--exclude-libs,ALL`) go straight into the JSON. One `aiml build -c exodus.shim.json -c exodus.worker.json -c exodus.json` builds everything. | Check that a hook can start programs (`sys.exe` in the JIT) and that `@file` arguments pass through `aiml build` unchanged. Nice to have in exodus: flags from a config tool (`llvm-config`, `pkg-config`) and dependencies between local projects. |
| `tools/gen_shim_header.py` | Generates `out/gen/oculus_shim.h` from the enums of `clang_shim.aiml` and the `extern: "C"` block of `inproc_frontend.aiml`, read line by line | Yes. Line reading, regex and string output are all there. It becomes `tools/gen_shim_header.aiml`, run by the build hook above. This removes the only Python of the CI build. | Nothing known. Optionally, an AIML way to read the enums of another AIML file (reflection) instead of parsing text. |
| `tests/run_tests.sh` | Runs `tests/unit/*.aiml` (JIT), the end-to-end checks (exit code plus expected output) and the catalogue for both frontends | Yes. `sys.exe` with capture and `returncode`, comparing against the expected files. The current `sh -c` cases (stderr thrown away, an exit code passed through) become plain options (`stderr: "/dev/null"`). | A diff output for failures has to be written (a line-by-line compare is enough). |
| `tests/run_catalog.sh` | Writes one `compile_commands.json` per bad and good case, runs oculus per rule, compares `file:line:col` against `expected.txt` | Yes. `io.list_dir`/`io.glob`, JSON written with `sys.serialize`, `sys.exe`, regex to pick findings, sort and compare. | Nothing known. |
| `tools/sync_catalog_tests.sh` | Copies `catalog/tests/<id>/` from a coding-rules checkout, skipping not-implemented rules | Yes. `io.copy`, `io.remove_all`, `io.mkdir_all`, regex on `not_implemented.txt`. | Check whether `io.copy` copies directory trees; if not, a small recursive walk. |
| `tools/run_real_projects.sh` | Clones yyjson, fmt, sqlite, imgui (and Zephyr) at pinned commits, configures with cmake/ninja, runs oculus, prints exit code, time and findings | Yes, as a driver of external programs (`git`, `cmake`, `ninja`, oculus) via `sys.exe`, `time.mono_ms` for the time. | git, cmake and ninja stay external tools; that is fine, no Python involved. spec/62 would later make it a cached matrix. |
| `tools/bench_frontends.py` | Times oculus per project and frontend and samples the peak RSS of the whole process tree from `/proc` every 10 ms | Mostly. `sys.exe` gives the pid; `running()` and a short `time.sleep` loop allow sampling; `/proc/<pid>/status` and `/proc/<pid>/task/*/children` are files that `io.read_file` can read. | Check that `/proc` files read correctly (size 0 in stat). A built-in peak RSS of a child (`getrusage(RUSAGE_CHILDREN)`) in `sys` would be simpler and more exact. Not needed in CI. |

Order: first `gen_shim_header` (removes Python from CI), then `worker/build.sh`
(removes the build shell script), then the test scripts. The real-project and
benchmark tools come last, since CI runs them only on request.

