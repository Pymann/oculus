#!/usr/bin/env bash
# Runs the unit tests (JIT) and the end-to-end tests against a built oculus.
#
# Usage: tests/run_tests.sh [path/to/oculus]     (default: out/oculus/oculus)
# Run from the repository root; `aiml` must be on PATH (`. <venv>/env`).
set -u

OCULUS="${1:-out/oculus/oculus}"
fails=0

for t in tests/unit/test_*.aiml; do
    if aiml "$t"; then :; else
        echo "FAIL $t (exit $?)"
        fails=$((fails + 1))
    fi
done

# expect <name> <want exit> <want stdout file or -> <command...>
expect() {
    local name="$1" want_rc="$2" want_out="$3"
    shift 3
    local out rc
    out="$("$@" 2>&1)"
    rc=$?
    if [ "$rc" -ne "$want_rc" ]; then
        echo "FAIL $name: exit $rc, want $want_rc"; echo "$out"
        fails=$((fails + 1)); return
    fi
    if [ "$want_out" != "-" ] && [ "$out" != "$(cat "$want_out")" ]; then
        echo "FAIL $name: output differs"; diff <(echo "$out") "$want_out"
        fails=$((fails + 1)); return
    fi
    echo "PASS $name"
}

FIX=tests/e2e/fixture
FB=tests/e2e/fallback
expect version 0 - "$OCULUS" --version
expect no-input 2 - "$OCULUS"
expect bad-format 2 - "$OCULUS" -p "$FIX" --format xml
expect bad-frontend 2 - "$OCULUS" -p "$FIX" --frontend xml

# Both Clang paths (doc/inprocess_frontend.md) must give the same results.
for fe in inproc json; do
    expect "text/$fe" 1 tests/e2e/expected_text.txt "$OCULUS" -p "$FIX" --config "$FIX/oculus.json" --frontend $fe
    expect "csv/$fe" 1 tests/e2e/expected_csv.txt "$OCULUS" -p "$FIX" --config "$FIX/oculus.json" --format csv --frontend $fe
    expect "threshold/$fe" 0 - "$OCULUS" -p "$FIX" --config "$FIX/oculus.json" --threshold error --frontend $fe
    expect "default-limit/$fe" 0 - "$OCULUS" -p "$FIX" --frontend $fe
    expect "paths/$fe" 0 - "$OCULUS" "$FIX/short.cpp" --frontend $fe

    # Plain paths without compile_commands.json: the fallback flags must reach Clang.
    expect "fallback-cli/$fe" 1 tests/e2e/expected_fallback.txt sh -c "\"$OCULUS\" --frontend $fe -I $FB/inc -D WITH_FIELDS --std c11 $FB/main.c 2>/dev/null"
    expect "fallback-config/$fe" 1 tests/e2e/expected_fallback.txt sh -c "\"$OCULUS\" --frontend $fe --config $FB/oculus.json $FB/main.c 2>/dev/null"
    expect "fallback-no-define/$fe" 0 - sh -c "\"$OCULUS\" --frontend $fe -I $FB/inc $FB/main.c 2>/dev/null"
    # Without the include path the unit does not compile: no findings, exit 2.
    expect "unclean-no-findings/$fe" 2 - sh -c "out=\$(\"$OCULUS\" --frontend $fe -D WITH_FIELDS $FB/main.c 2>/dev/null); rc=\$?; [ -z \"\$out\" ] && exit \$rc; echo \"\$out\"; exit 9"

    # Rule keys of oculus.json: names for OC-0255 and OC-0294, handlers_persist
    # for OC-0470 (tests/e2e/rule_keys).
    RK=tests/e2e/rule_keys
    for rk in "255 pointer_count.cpp c++20" "294 late_lookup.cpp c++20" "470 reinstall.c c17"; do
        set -- $rk
        expect "rule-key-$1-off/$fe" 1 - sh -c "\"$OCULUS\" --frontend $fe --std $3 --single-rule $1 $RK/$2 2>/dev/null | grep -q OC-0$1 && exit 1"
        expect "rule-key-$1-on/$fe" 0 - sh -c "\"$OCULUS\" --frontend $fe --std $3 --single-rule $1 --config $RK/oculus.json $RK/$2 2>/dev/null | grep -q OC-0$1 && exit 1; exit 0"
    done

    if tests/run_catalog.sh "$OCULUS" $fe; then :; else fails=$((fails + 1)); fi
done

# Without its JSON worker oculus must fail the unit, not report it clean.
lonely="$(mktemp -d)"
mkdir -p "$lonely/bin" "$lonely/lib"
cp "$OCULUS" "$lonely/bin/oculus"
cp "$(dirname "$OCULUS")/../lib/liboculus_clang.so" "$lonely/lib/"
expect no-worker 2 - "$lonely/bin/oculus" "$FIX/short.cpp" --frontend json
rm -rf "$lonely"

if [ "$fails" -gt 0 ]; then echo "$fails test(s) failed"; exit 1; fi
echo "all tests passed"
