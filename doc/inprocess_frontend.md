# In-process Clang frontend

Oculus reaches Clang in one of two ways. Both deliver the same RuleInformation
(`doc/ir.md`) and are tested against the same expectations:

| `--frontend` | How | Status |
|---|---|---|
| `inproc` (default) | `liboculus_clang.so` parses the unit inside the oculus process | default |
| `json` | one `oculus-clang` process per unit prints JSON, read with `sys.deserialize` | kept as the reference and as the fallback |

The in-process path saves the process start per unit and the JSON round trip
(writing, reading and deserializing every record). Parsing itself costs the same
on both paths. For the measurements see "Measurements" at the end.

## 1. The C interface

AIML calls C only (aiml spec/35), so the shim is a thin `extern "C"` layer over
LibTooling. The interface is specified in AIML. The C header is generated from it.

- `src/frontend/clang_shim.aiml`: the field ids, as `wire` enums (`ClangUnitText`,
  `ClangDeclNum`, `ClangPosWhich`, ...), and the ABI version `ClangShimAbi.version`.
- `src/frontend/inproc_frontend.aiml`: the `extern: "C"` block with the 19 functions.
  It lives there because an `extern` declaration is visible only in its own file.
- `tools/gen_shim_header.py` turns both into `out/gen/oculus_shim.h`.
  `worker/oculus_shim.cpp` includes that header, so a definition that differs from
  the specification does not compile. A `static_assert` ties the ABI version to the
  shim.

The functions:

| Function | Returns |
|---|---|
| `oc_shim_abi()` | interface version; oculus refuses a shim with another one |
| `oc_parse(directory, file, argv, command)` | opaque unit handle (`ptr`). `argv` holds one argument per line; `command` is used when `argv` is empty |
| `oc_unit_free(unit)` | releases the unit (null-safe) |
| `oc_unit_text/num(unit, field)` | unit fields and the table sizes |
| `oc_dropped_flag(unit, index)` | compiler flags Clang did not know (dropped, unit parsed again) |
| `oc_file_text/num`, `oc_type_text/num`, `oc_decl_text/num(unit, index, field)` | one field of one record |
| `oc_decl_pos(unit, index, which, part)` | one part (`file_uid`, `line`, `column`) of one position of a declaration |
| `oc_node_text/num/pos`, `oc_macro_text/num/pos` | the same for statement nodes and preprocessor records |

The rules of the interface:

- **Copied, not borrowed.** `oc_parse` parses with a frontend action of its own: the
  preprocessor callbacks record the macros and directives while Clang parses, and
  when Sema is done the AST is walked once and copied into records
  (`worker/unit_tables.h`, the same code the JSON worker uses). The AST is freed
  before `oc_parse` returns. A unit handle holds plain records, never a Clang pointer.
- **Null-safe and range-checked.** A null unit, a wrong index or a wrong field
  gives `""` or `-1`, never a crash.
- **Strings are AIML strings.** The shim allocates them with `aiml_str_dup` of
  `libaiml.so`, and AIML takes ownership. A test with 20 million calls showed
  no growth in memory.
- **Fields over structs.** One accessor per field instead of struct layouts keeps
  the interface independent of C struct layouts and of the AIML model layout.

`inproc_frontend.aiml` builds `IrFile`, `IrType` and `IrDecl` from the accessors.
The rules see no difference between the paths.

## 2. Symbol hiding: two LLVMs in one process

The `oculus` binary loads `libaiml.so`, and `libaiml.so` links `libLLVM.so.18.1` at
run time. Oculus itself uses the newest stable LLVM (22 in CI). Two LLVMs in one
process are safe only if neither side can see the other's symbols:

- The LLVM packages version their exported symbols (`LLVM_18.1` vs `LLVM_22.1`), so
  the two shared `libLLVM.so` would not clash with each other.
- `libaiml.so` also exports about 1200 *unversioned* weak LLVM 18 template
  instances (`SmallVector`, `cast`, ...). The dynamic loader would bind a shared
  LLVM 22 to them. That breaks the one-definition rule and is undefined behaviour.

Therefore `worker/build.sh` links the shim like this:

- LLVM and Clang **static** (`libclang*.a` plus `llvm-config --link-static --libs`).
- `-fvisibility=hidden`, `--exclude-libs,ALL` and `-Bsymbolic`.
- The version script `worker/oculus_shim.map`, which exports `oc_*` and nothing else.

CI checks that the shim exports only `oc_*`. The shim's only undefined
non-system symbol is `aiml_str_dup`. `OCULUS_SHIM_LINK=shared` links against the
shared libraries instead, which is safe only when the LLVM version matches the one
of libaiml.so.

Nothing in AIML had to change. `exodus.json` links `-loculus_clang` (from
`out/lib`, rpath `$ORIGIN/../lib`).

## 3. Build layout

`worker/build.sh` runs **before** `aiml build`, because the binary links the shim:

```
out/gen/oculus_shim.h          generated
out/lib/liboculus_clang.so     the shim (static LLVM/Clang, about 45 MB)
out/lib/oculus-clang           the JSON worker
out/oculus/oculus              aiml build
```

The JSON worker is looked up next to `oculus` first, then in `../lib`.

## 4. Process model and crash isolation

For now `inproc` parses every unit in the oculus process, one after another. A
Clang crash (rare, but real on unusual code) would end the whole scan. The plan,
not built yet:

- `oculus` starts itself again per unit, or per batch of units, in a worker mode
  (`--unit-worker`). The child parses in-process, runs the rules and prints its
  findings and unit problems as plain lines. The parent merges them and
  deduplicates as today.
- A crashed child costs only its units; the parent reports them as problems.
- This is the same structure as the JSON worker, but without JSON. The child runs the
  rules itself, so no RuleInformation crosses the process boundary.
- This also gives the parallel workers of `doc/architecture.md` section 3
  (`--jobs`).

Phase 2 (cross-TU facts) then needs a compact fact format from child to parent,
not the whole RuleInformation.

## 5. Measurements

`tools/bench_frontends.py` runs `oculus -p BUILD --single-rule 40` with both
frontends and records the wall time and the peak RSS of the whole process tree.
The results are in `doc/bench/frontends.csv`, the chart is `doc/bench/frontends.png`.

Setup: 2026-11-26, 4 cores, LLVM 18 on both paths, units one after another, median
of 3 runs.

| Project | Units | Time JSON | Time in-process | | Peak RAM JSON | Peak RAM in-process | |
|---|---|---|---|---|---|---|---|
| yyjson | 19 | 4.5 s | 2.1 s | -54 % | 171 MiB | 108 MiB | -36 % |
| fmt | 52 | 140.6 s | 70.4 s | -50 % | 381 MiB | 283 MiB | -26 % |
| Zephyr sample | 77 | 16.6 s | 9.2 s | -44 % | 178 MiB | 109 MiB | -39 % |

The findings are identical on both paths (yyjson, Zephyr).

Where the time goes: on `fmt/src/format.cc` the worker needs 1.0 s to parse and
write its JSON, which is 8.2 MB. On average a fmt unit costs 2.7 s on the JSON path
and 1.35 s in-process. So most of the saving is the reading of the JSON
(`sys.deserialize`), not the process start. The RAM saving is the second process
plus the JSON text held on both sides.
