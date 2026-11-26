#!/usr/bin/env bash
# Copies the generated test cases of the given rule ids from a checkout of
# Pymann/coding-rules (catalog/tests/<id>/) into tests/catalog/.  The examples are
# Oculus' own catalogue text; the copy keeps CI independent of that private repo.
#
# Usage: tools/sync_catalog_tests.sh CODING_RULES_DIR ID...      (ID like 0040)
set -eu
src="$1"; shift
for id in "$@"; do
    rm -rf "tests/catalog/$id"
    mkdir -p "tests/catalog/$id"
    cp -r "$src/catalog/tests/$id/." "tests/catalog/$id/"
    echo "synced $id"
done
