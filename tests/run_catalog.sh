#!/usr/bin/env bash
# Runs the catalogue test cases of tests/catalog/<id>/<language>/ (copied from
# Pymann/coding-rules by tools/sync_catalog_tests.sh) against a built oculus.
#
# Per case: the `bad` project (bad* files plus helper files) must give exactly the
# findings of expected.txt (file:line:col) for that rule, the `good` project
# (good* files plus helpers) none.  Each project gets its own
# compile_commands.json with the standard of std.txt.
#
# Usage: tests/run_catalog.sh [path/to/oculus]
set -u
OCULUS="$(realpath "${1:-out/oculus/oculus}")"
fails=0
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# project <dir> <prefix> <std> <out compile_commands.json>
write_db() {
    local dir="$1" prefix="$2" std="$3" first=1 file driver
    printf '[\n'
    for file in "$dir"/*.c "$dir"/*.cpp; do
        [ -e "$file" ] || continue
        file="$(basename "$file")"
        case "$file" in "$prefix"*) ;; bad*|good*) continue ;; *) ;; esac
        case "$file" in *.cpp) driver=clang++ ;; *) driver=clang ;; esac
        [ $first -eq 1 ] || printf ',\n'
        first=0
        printf '  { "directory": "%s", "file": "%s", "arguments": ["%s", "-std=%s", "-c", "%s"] }' \
            "$dir" "$file" "$driver" "$std" "$file"
    done
    printf '\n]\n'
}

for case_dir in tests/catalog/*/*/; do
    case_dir="$(realpath "$case_dir")"
    id="$(basename "$(dirname "$case_dir")")"
    lang="$(basename "$case_dir")"
    rule=$((10#$id))
    std="$(cat "$case_dir/std.txt")"
    for prefix in bad good; do
        db="$work/$id-$lang-$prefix"
        mkdir -p "$db"
        write_db "$case_dir" "$prefix" "$std" > "$db/compile_commands.json"
        config=()
        [ -f "$case_dir/oculus.json" ] && config=(--config "$case_dir/oculus.json")
        out="$("$OCULUS" -p "$db" --single-rule "$rule" "${config[@]}" 2>&1)"
        rc=$?
        if [ $rc -eq 2 ]; then
            echo "FAIL $id/$lang $prefix: oculus error"; echo "$out"; fails=$((fails + 1)); continue
        fi
        got="$(printf '%s\n' "$out" | sed -n "s|^$case_dir/\([^:]*:[0-9]*:[0-9]*\): OC-$id: .*|\1|p" | sort)"
        if [ "$prefix" = bad ]; then want="$(sort "$case_dir/expected.txt")"; else want=""; fi
        if [ "$got" != "$want" ]; then
            echo "FAIL $id/$lang $prefix: findings differ"
            diff <(printf '%s\n' "$want") <(printf '%s\n' "$got")
            printf '%s\n' "$out"
            fails=$((fails + 1)); continue
        fi
        echo "PASS $id/$lang $prefix ($(printf '%s' "$want" | grep -c . ) findings)"
    done
done
if [ "$fails" -gt 0 ]; then echo "$fails catalogue case(s) failed"; exit 1; fi
echo "all catalogue cases passed"
