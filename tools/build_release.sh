#!/usr/bin/env bash
# Build Atlas SDK for distribution to external (non-Bazel) projects.
# Uses MediaPipe-style cc_binary(linkshared=True, linkstatic=True) +
# ATLAS_API-decorated public classes for self-contained shared library.
#
# Usage:
#   ./tools/build_release.sh                        # Build for host
#   ./tools/build_release.sh --platform linux_aarch64
#   ./tools/build_release.sh --list-platforms
#   ./tools/build_release.sh --help
#
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX="${REPO_ROOT}/atlas-sdk"
PLATFORM=""
SHOW_PLATFORMS=false
SHOW_HELP=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform)       PLATFORM="$2";       shift 2 ;;
        --prefix)         PREFIX="$2";         shift 2 ;;
        --list-platforms) SHOW_PLATFORMS=true; shift   ;;
        --help|-h)        SHOW_HELP=true;      shift   ;;
        *) echo "ERROR: Unknown option '$1'"; exit 1 ;;
    esac
done

if [[ "${SHOW_HELP}" == true ]]; then
    sed -n '2,11p' "$0" | sed 's/^# //'
    exit 0
fi

if [[ "${SHOW_PLATFORMS}" == true ]]; then
    echo "Supported platforms (from .bazelrc):"
    echo ""
    for name in macos_arm64 macos_x86_64 linux_x86_64 linux_aarch64 android_arm64 android_x86_64; do
        desc=""; case "$name" in
            macos_arm64)    desc="macOS Apple Silicon" ;; macos_x86_64)   desc="macOS Intel" ;;
            linux_x86_64)   desc="Linux x86-64" ;;        linux_aarch64)  desc="Linux AArch64" ;;
            android_arm64)  desc="Android ARM64-v8a" ;;  android_x86_64) desc="Android x86-64" ;;
        esac
        printf "  %-18s → %s\n" "$name" "$desc"
    done
    echo ""; echo "Use: ./tools/build_release.sh --platform <name>"; exit 0
fi

BAZEL_CONFIG=""
PLATFORM_DIR="host"
if [[ -n "${PLATFORM}" ]]; then
    BAZEL_CONFIG="--config=${PLATFORM}"
    PLATFORM_DIR="$(echo "${PLATFORM}" | tr '_' '-')"
fi

OUTPUT_DIR="${PREFIX}/${PLATFORM_DIR}"
echo "═══ Atlas SDK Build ═══"
echo "  Platform:      ${PLATFORM:-host}"
echo "  Output:        ${OUTPUT_DIR}"

echo "[1/2] Building //src/public:atlas_shared ..."
# shellcheck disable=SC2086
bazel build //src/public:atlas_shared ${BAZEL_CONFIG}

echo "[2/2] Packaging SDK ..."
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}/include/atlas" "${OUTPUT_DIR}/lib/pkgconfig"

# Headers
cp "${REPO_ROOT}/src/public/include/atlas/"*.h "${OUTPUT_DIR}/include/atlas/"

# Self-contained shared library (MediaPipe-style)
cp "${REPO_ROOT}/bazel-bin/src/public/libatlas_shared.dylib" \
   "${OUTPUT_DIR}/lib/libatlas.1.0.0.dylib"
ln -sf libatlas.1.0.0.dylib "${OUTPUT_DIR}/lib/libatlas.1.dylib"
ln -sf libatlas.1.dylib     "${OUTPUT_DIR}/lib/libatlas.dylib"
install_name_tool -id @rpath/libatlas.1.dylib "${OUTPUT_DIR}/lib/libatlas.1.0.0.dylib" 2>/dev/null || true

# ONNX Runtime
ORT_LIB=$(find "$(bazel info output_base 2>/dev/null || echo /private/var/tmp/_bazel_moks)" \
    -name "libonnxruntime.1.17.3.dylib" -type f 2>/dev/null | head -1)
if [[ -n "${ORT_LIB}" ]]; then
    cp "${ORT_LIB}" "${OUTPUT_DIR}/lib/"
fi

# pkg-config
sed "s|@PREFIX@|${OUTPUT_DIR}|g" "${REPO_ROOT}/tools/atlas.pc.in" \
    > "${OUTPUT_DIR}/lib/pkgconfig/atlas.pc"

echo ""
echo "═══ SDK ready: ${OUTPUT_DIR} ═══"
echo "  include/atlas/  $(ls "${OUTPUT_DIR}/include/atlas/" | wc -l) headers"
echo "  lib/            $(ls "${OUTPUT_DIR}/lib/"*.dylib 2>/dev/null | wc -l | tr -d ' ') dylibs"
echo ""
echo "To test:"
echo "  export ATLAS_SDK=${OUTPUT_DIR}"
echo "  cd tests/external_consumer && make && make test"