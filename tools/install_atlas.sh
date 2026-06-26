#!/usr/bin/env bash
# Installs Atlas headers, library, and runtime deps to a prefix.
# Usage: ./tools/install_atlas.sh /usr/local

set -euo pipefail

PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[1/4] Building libatlas shared library ..."
bazel build //src/public:atlas

echo "[2/4] Installing headers ..."
mkdir -p "${PREFIX}/include/atlas"
cp "${REPO_ROOT}/src/public/include/atlas/"*.h "${PREFIX}/include/atlas/"

echo "[3/4] Installing library ..."
mkdir -p "${PREFIX}/lib"
# On macOS the output is libatlas.dylib, on Linux libatlas.so.
LIB_FILE=""
for candidate in bazel-bin/src/public/libatlas.so bazel-bin/src/public/libatlas.dylib; do
    if [[ -f "${candidate}" ]]; then
        LIB_FILE="${candidate}"
        break
    fi
done
if [[ -z "${LIB_FILE}" ]]; then
    echo "ERROR: Shared library not found in bazel-bin/src/public/"
    exit 1
fi
LIB_EXT="${LIB_FILE##*.}"
cp "${LIB_FILE}" "${PREFIX}/lib/libatlas.${LIB_EXT}.1.0.0"
ln -sf "libatlas.${LIB_EXT}.1.0.0" "${PREFIX}/lib/libatlas.${LIB_EXT}.1"
ln -sf "libatlas.${LIB_EXT}.1"     "${PREFIX}/lib/libatlas.${LIB_EXT}"

# Copy ONNX Runtime shared library
ORT_LIB=$(find bazel-bin/external -name "libonnxruntime.so*" -o -name "libonnxruntime*.dylib*" 2>/dev/null | head -1 || true)
if [[ -n "${ORT_LIB}" ]]; then
    mkdir -p "${PREFIX}/lib/atlas"
    cp "${ORT_LIB}" "${PREFIX}/lib/atlas/"
fi

echo "[4/4] Installing pkg-config ..."
mkdir -p "${PREFIX}/lib/pkgconfig"
sed "s|@PREFIX@|${PREFIX}|g" "${REPO_ROOT}/tools/atlas.pc.in" > "${PREFIX}/lib/pkgconfig/atlas.pc"

echo "Atlas installed to ${PREFIX}"
