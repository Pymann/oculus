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
expect version 0 - "$OCULUS" --version
expect no-input 2 - "$OCULUS"
expect bad-format 2 - "$OCULUS" -p "$FIX" --format xml
expect text 1 tests/e2e/expected_text.txt "$OCULUS" -p "$FIX" --config "$FIX/oculus.json"
expect csv 1 tests/e2e/expected_csv.txt "$OCULUS" -p "$FIX" --config "$FIX/oculus.json" --format csv
expect threshold 0 - "$OCULUS" -p "$FIX" --config "$FIX/oculus.json" --threshold error
expect default-limit 0 - "$OCULUS" -p "$FIX"
expect paths 0 - "$OCULUS" "$FIX/short.cpp"

# Plain paths without compile_commands.json: the fallback flags must reach Clang.
FB=tests/e2e/fallback
expect fallback-cli 1 tests/e2e/expected_fallback.txt sh -c "\"$OCULUS\" -I $FB/inc -D WITH_FIELDS --std c11 $FB/main.c 2>/dev/null"
expect fallback-config 1 tests/e2e/expected_fallback.txt sh -c "\"$OCULUS\" --config $FB/oculus.json $FB/main.c 2>/dev/null"
expect fallback-no-define 0 - sh -c "\"$OCULUS\" -I $FB/inc $FB/main.c 2>/dev/null"

# Without its Clang worker oculus must fail the unit, not report it clean.
lonely="$(mktemp -d)"
cp "$OCULUS" "$lonely/oculus"
expect no-worker 2 - "$lonely/oculus" "$FIX/short.cpp"
rm -rf "$lonely"

if tests/run_catalog.sh "$OCULUS"; then :; else fails=$((fails + 1)); fi

if [ "$fails" -gt 0 ]; then echo "$fails test(s) failed"; exit 1; fi
echo "all tests passed"
