#!/usr/bin/env bash
# Builds the Clang worker out/oculus/oculus-clang against the LLVM that
# `llvm-config` (or $LLVM_CONFIG) names.  Run from the repository root.
set -eu
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"
bindir="$("$LLVM_CONFIG" --bindir)"
resource_dir="$("$bindir/clang" -print-resource-dir)"
mkdir -p out/oculus
# shellcheck disable=SC2046
"${CXX:-$bindir/clang++}" -std=c++17 -O2 -fno-rtti \
    $("$LLVM_CONFIG" --cxxflags | sed -e 's/-fno-exceptions//' -e 's/-std=c++[0-9]*//') \
    -DOCULUS_RESOURCE_DIR="\"$resource_dir\"" \
    worker/oculus_clang.cpp -o out/oculus/oculus-clang \
    $("$LLVM_CONFIG" --ldflags) -lclang-cpp $("$LLVM_CONFIG" --link-shared --libs) \
    -Wl,-rpath,"$("$LLVM_CONFIG" --libdir)"
echo "built out/oculus/oculus-clang ($("$LLVM_CONFIG" --version), resource dir $resource_dir)"
