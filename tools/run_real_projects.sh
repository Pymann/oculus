#!/usr/bin/env bash
# Runs a built oculus on real open-source projects, at pinned commits, and prints
# per project: exit code, time, findings of the rule, compile warnings.  Nothing of
# these projects is committed; they are cloned into WORK_DIR.
#
# Usage: tools/run_real_projects.sh [WORK_DIR] [RULE_ID]    (default /tmp/oculus-real, 40)
#   ZEPHYR=1 also builds the Zephyr synchronization sample for native_sim/native/64
#   (needs gcc, device-tree-compiler, gperf and Zephyr's Python requirements).
set -u
OCULUS="$(realpath out/oculus/oculus)"
WORK="${1:-/tmp/oculus-real}"
RULE="${2:-40}"
mkdir -p "$WORK"
cd "$WORK" || exit 2

fetch() {   # fetch <dir> <url> <commit>
    if [ ! -d "$1" ]; then
        git init -q "$1" && git -C "$1" remote add origin "$2"
    fi
    git -C "$1" fetch -q --depth 1 origin "$3" && git -C "$1" checkout -q FETCH_HEAD
}

run() {     # run <name> <dir> <oculus args...>
    local name="$1" dir="$2" start end rc
    shift 2
    start=$(date +%s)
    (cd "$dir" && "$OCULUS" --single-rule "$RULE" "$@") > "$WORK/$name.out" 2> "$WORK/$name.err"
    rc=$?
    end=$(date +%s)
    echo "== $name: exit $rc, $((end - start)) s, $(grep -c ": OC-" "$WORK/$name.out") finding(s)"
    grep -E "warning: [0-9]+ unit|note:" "$WORK/$name.err" | head -3
    head -20 "$WORK/$name.out"
}

fetch yyjson https://github.com/ibireme/yyjson.git be9bd74bb3c96e00006c2075c330a6b8b90123dc
cmake -S yyjson -B yyjson/build -G Ninja -DCMAKE_C_COMPILER=clang -DYYJSON_BUILD_TESTS=ON \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
run yyjson . -p yyjson/build

fetch fmt https://github.com/fmtlib/fmt.git 22b7d55ac8d105edc8461e8b84ac66eefe4c802e
cmake -S fmt -B fmt/build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DFMT_TEST=ON \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
run fmt . -p fmt/build

# No build system: scanned as plain paths with fallback flags.
fetch sqlite https://github.com/azadkuh/sqlite-amalgamation.git 15d0ff10ebc7e7225eced1de84bb52137000899b
run sqlite sqlite --std c11 -D _GNU_SOURCE sqlite3.c shell.c

fetch imgui https://github.com/ocornut/imgui.git 0baafdcd3870bcdf5714a81ee2e4b1cc90ed134c
run imgui imgui --std c++17 -I . imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp

if [ "${ZEPHYR:-0}" = 1 ]; then
    fetch zephyr https://github.com/zephyrproject-rtos/zephyr.git 17bf798be778141eac04c61fae6633a14019069d
    ZEPHYR_BASE="$WORK/zephyr" ZEPHYR_TOOLCHAIN_VARIANT=host \
        cmake -G Ninja -S zephyr/samples/synchronization -B zbuild -DBOARD=native_sim/native/64 > zbuild.log 2>&1 \
        && ninja -C zbuild >> zbuild.log 2>&1 || { echo "== zephyr: build failed, see $WORK/zbuild.log"; exit 1; }
    run zephyr . -p zbuild
fi
