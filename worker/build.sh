#!/usr/bin/env bash
# Builds the Clang side of Oculus against the LLVM that `llvm-config` (or
# $LLVM_CONFIG) names.  Run from the repository root, before `aiml build`: the
# oculus binary links the shim.
#
#   out/gen/oculus_shim.h        generated from src/frontend/clang_shim.aiml and the
#                                extern block of src/frontend/inproc_frontend.aiml
#   out/lib/liboculus_clang.so   the in-process shim (doc/inprocess_frontend.md),
#                                LLVM and Clang linked statically, only oc_* exported
#   out/lib/oculus-clang         the JSON worker (`oculus --frontend json`)
#
# OCULUS_SHIM_LINK=shared links the shim against the shared libclang-cpp/libLLVM
# instead; only safe when that is the LLVM version libaiml.so uses.
set -eu
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"
bindir="$("$LLVM_CONFIG" --bindir)"
libdir="$("$LLVM_CONFIG" --libdir)"
resource_dir="$("$bindir/clang" -print-resource-dir)"
CXX="${CXX:-$bindir/clang++}"
mkdir -p out/gen out/lib
python3 tools/gen_shim_header.py out/gen/oculus_shim.h src/frontend/clang_shim.aiml src/frontend/inproc_frontend.aiml

cxxflags=(-std=c++17 -O2 -fno-rtti -fPIC -DOCULUS_RESOURCE_DIR="\"$resource_dir\"" -Iworker -Iout/gen)
# shellcheck disable=SC2207
cxxflags+=($("$LLVM_CONFIG" --cxxflags | sed -e 's/-fno-exceptions//' -e 's/-std=c++[0-9]*//'))

# shellcheck disable=SC2046
"$CXX" "${cxxflags[@]}" worker/oculus_clang.cpp -o out/lib/oculus-clang \
    $("$LLVM_CONFIG" --ldflags) -lclang-cpp $("$LLVM_CONFIG" --link-shared --libs) \
    -Wl,-rpath,"$libdir"
echo "built out/lib/oculus-clang"

if [ "${OCULUS_SHIM_LINK:-static}" = "shared" ]; then
    # shellcheck disable=SC2046
    clang_libs=(-lclang-cpp $("$LLVM_CONFIG" --link-shared --libs) -Wl,-rpath,"$libdir")
else
    # Every libclang*.a in one group: the linker takes only the objects it needs.
    # Polly is a plugin of the optimizer, not needed for parsing (and not always installed).
    # shellcheck disable=SC2046
    clang_libs=(-Wl,--start-group $(ls "$libdir"/libclang[A-Z]*.a) -Wl,--end-group
                $("$LLVM_CONFIG" --link-static --libs | tr ' ' '\n' | grep -v Polly)
                $("$LLVM_CONFIG" --link-static --system-libs)
                -Wl,--exclude-libs,ALL)
fi
"$CXX" "${cxxflags[@]}" -fvisibility=hidden -fvisibility-inlines-hidden -shared \
    worker/oculus_shim.cpp -o out/lib/liboculus_clang.so \
    $("$LLVM_CONFIG" --ldflags) "${clang_libs[@]}" \
    -Wl,--version-script=worker/oculus_shim.map -Wl,-Bsymbolic -Wl,--gc-sections
echo "built out/lib/liboculus_clang.so ($("$LLVM_CONFIG" --version), ${OCULUS_SHIM_LINK:-static}, resource dir $resource_dir)"
