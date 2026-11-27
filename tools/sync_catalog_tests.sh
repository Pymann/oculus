#!/usr/bin/env bash
# Copies the generated test cases of the given rule ids from a checkout of
# Pymann/coding-rules (catalog/tests/<id>/) into tests/catalog/.  The examples are
# Oculus' own catalogue text; the copy keeps CI independent of that private repo.
#
# Rules the catalogue marks as not implemented (catalog/not_implemented.txt, e.g.
# OC-0386) are skipped.
#
# Usage: tools/sync_catalog_tests.sh CODING_RULES_DIR ID...      (ID like 0040)
set -eu
src="$1"; shift
for id in "$@"; do
    if grep -qE "^0*${id#"${id%%[!0]*}"} *\|" "$src/catalog/not_implemented.txt" 2>/dev/null; then
        echo "skipped $id: not implemented (catalog/not_implemented.txt)"
        continue
    fi
    rm -rf "tests/catalog/$id"
    mkdir -p "tests/catalog/$id"
    cp -r "$src/catalog/tests/$id/." "tests/catalog/$id/"
    echo "synced $id"
done
