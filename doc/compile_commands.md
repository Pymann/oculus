# Getting a compile_commands.json

Oculus parses every translation unit with exactly the flags the build uses. Those
come from a compilation database, `compile_commands.json`, passed with
`-p BUILD_DIR`. Most build systems can write one:

| build | how |
|---|---|
| CMake | `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then `oculus -p build` |
| Meson | written by default into the build directory |
| Ninja (without CMake) | `ninja -t compdb > compile_commands.json` |
| Make, autotools, scripts | record the build with Bear: `bear -- make`, or with `compiledb make` |
| Zephyr (west) | every `west build` writes `build/compile_commands.json` (CMake) |
| Bazel | the `hedron_compile_commands` rule |
| IDE projects (Keil, IAR, Eclipse) | export the project, or run the IDE's command-line build under Bear |

## Without a compilation database

Oculus also takes plain source paths. It then builds one compiler command per file
from these flags, and prints a warning that results may be less accurate:

```
oculus -I include -I third_party -D NDEBUG -D BOARD=2 --std c11 src/*.c
```

or, in `oculus.json`:

```json
{ "fallback": { "include_dirs": ["include"], "defines": ["NDEBUG"],
                "std": "c11", "flags": ["-fno-builtin"] } }
```

The command line adds `-I` and `-D` to the ones of `oculus.json`, and its `--std`
wins. `.c` files are parsed as C, `.cc`, `.cpp`, `.cxx`, `.C` and C++ headers as C++.
What the fallback cannot know: flags that differ from file to file, defines set by
the build system or the toolchain (`-DCONFIG_...`, target macros of a cross compiler),
and the target itself. Missing include paths show up as unresolved types; a rule
then sees less than in the real build, so it reports less, not more.
