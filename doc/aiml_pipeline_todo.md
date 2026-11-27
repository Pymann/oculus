# TODO: build and test pipeline in AIML

Infrastructure work that is not about rules. Nothing here is done yet.

## Build and test scripts in AIML (no Python, no shell scripts)

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

## Crash isolation

- **Crash isolation** for the in-process Clang frontend
  (doc/inprocess_frontend.md section 4) is not built yet. A Clang crash on unusual
  code ends the whole scan. Workaround: `--frontend json`.
