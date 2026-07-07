#!/bin/bash
# Usage:
#   source tools/snpe_cpu_shared.sh env    # setup conda snpe36 + env vars (must source)
#   bash tools/snpe_cpu_shared.sh gen      # generate DLC + ONNX models
#   bash tools/snpe_cpu_shared.sh build    # cross-compile Android arm64 binary
#   bash tools/snpe_cpu_shared.sh push     # push resources to Android device
#   bash tools/snpe_cpu_shared.sh run      # run on Android device
#   bash tools/snpe_cpu_shared.sh all      # gen + build + push + run
#
# SNPE is only used for Android arm64 build.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ---------------------------------------------------------------------------
# Configuration (override via environment)
# ---------------------------------------------------------------------------
HOST_MODEL_DIR="${HOST_MODEL_DIR:-/tmp/snpe_cpu_shared_models}"
DEVICE_APP_DIR="${DEVICE_APP_DIR:-/data/local/tmp/snpe_cpu_shared}"
DEVICE_LIB_DIR="${DEVICE_LIB_DIR:-/system/lib64}"

ATLAS_SDK_ANDROID="${REPO_ROOT}/atlas-sdk/android-arm64"

# Auto-detect CROSS_COMPILE from ANDROID_NDK_HOME if not explicitly set
if [[ -z "${CROSS_COMPILE:-}" ]]; then
    if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
        NDK_TOOLCHAIN="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin"
        CROSS_COMPILE="${NDK_TOOLCHAIN}/aarch64-linux-android24-"
    else
        CROSS_COMPILE="aarch64-linux-android24-"
    fi
fi

ADB_BIN="${ADB_BIN:-adb}"
ADB_SERIAL="${ADB_SERIAL:-}"

SAMPLE_DIR="${REPO_ROOT}/examples/snpe_cpu_shared"
MANIFEST_SRC="${SAMPLE_DIR}/manifest.json"
ONNX_SRC="${SAMPLE_DIR}/relu.onnx"
DLC_SRC="${SAMPLE_DIR}/relu_snpe.dlc"

adb_exec() {
    if [[ -n "${ADB_SERIAL}" ]]; then
        "${ADB_BIN}" -s "${ADB_SERIAL}" "$@"
    else
        "${ADB_BIN}" "$@"
    fi
}

require_host_file() {
    local path="$1"
    local label="$2"
    if [[ ! -f "${path}" ]]; then
        echo "ERROR: ${label} not found: ${path}"
        exit 1
    fi
}

help() {
    echo "Usage:"
    echo "  source $0 env     - setup conda snpe36 + env vars (must source)"
    echo "  bash $0 gen       - generate DLC + ONNX models"
    echo "  bash $0 build     - cross-compile Android arm64 binary"
    echo "  bash $0 push      - push resources to Android device"
    echo "  bash $0 run       - run on Android device"
    echo "  bash $0 all       - gen + build + push + run"
}

# ============================================================================
# env  — Activate conda snpe36 and export environment variables
# ============================================================================
cmd_env() {
    # Ensure conda snpe36 is active
    if [[ "${CONDA_DEFAULT_ENV:-}" != "snpe36" ]]; then
        echo "Activating conda env: snpe36 ..."
        # shellcheck disable=SC1091
        source "$(conda info --base)/etc/profile.d/conda.sh"
        conda activate snpe36
    fi

    # Export SNPE SDK path (assumes SNPE SDK is installed in conda env or default location)
    export SNPE_SDK_PATH="${SNPE_SDK_PATH:-/opt/qcom/sdk/snpe-1.50.0.2622}"
    export PYTHONPATH="${SNPE_SDK_PATH}/lib/python${PYTHONPATH:+:${PYTHONPATH}}"

    # Export Atlas SDK path for Android
    export ATLAS_SDK="${ATLAS_SDK:-${ATLAS_SDK_ANDROID}}"
    export CROSS_COMPILE

    # Export model directories
    export HOST_MODEL_DIR
    export SAMPLE_MODEL_DIR="${HOST_MODEL_DIR}"

    # Auto-detect NDK toolchain if ANDROID_NDK_HOME is set
    if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
        NDK_TOOLCHAIN="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin"
        # Only auto-set CROSS_COMPILE if user hasn't explicitly set it
        if [[ -z "${CROSS_COMPILE:-}" ]]; then
            CROSS_COMPILE="${NDK_TOOLCHAIN}/aarch64-linux-android24-"
        fi
        export ANDROID_NDK_HOME
    fi

    echo "=== SNPE CPU Shared — Environment ==="
    echo "  CONDA_DEFAULT_ENV=${CONDA_DEFAULT_ENV}"
    echo "  SNPE_SDK_PATH=${SNPE_SDK_PATH}"
    echo "  ATLAS_SDK=${ATLAS_SDK}"
    echo "  ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-<not set>}"
    echo "  CROSS_COMPILE=${CROSS_COMPILE}"
    echo "  HOST_MODEL_DIR=${HOST_MODEL_DIR}"
}

# ============================================================================
# gen  — Generate ONNX and DLC models
# ============================================================================
cmd_gen() {
    echo "=== Generating models ==="
    mkdir -p "${HOST_MODEL_DIR}"

    # gen_models.py handles both ONNX and DLC
    python3 "${SAMPLE_DIR}/gen_models.py" "${HOST_MODEL_DIR}"

    # Verify outputs
    require_host_file "${ONNX_SRC}" "ONNX model"
    require_host_file "${HOST_MODEL_DIR}/relu_snpe.dlc" "DLC model"

    echo "Models generated successfully."
    echo "  ONNX: ${ONNX_SRC}"
    echo "  DLC:  ${HOST_MODEL_DIR}/relu_snpe.dlc"
}

# ============================================================================
# build  — Cross-compile Android arm64 binary via Makefile
# ============================================================================
cmd_build() {
    echo "=== Building Android arm64 binary ==="
    echo "  ATLAS_SDK=${ATLAS_SDK}"
    echo "  CROSS_COMPILE=${CROSS_COMPILE}"

    # Verify SDK exists
    if [[ ! -d "${ATLAS_SDK}" ]]; then
        echo "ERROR: ATLAS_SDK directory not found: ${ATLAS_SDK}"
        echo "  Build the SDK first: ./tools/build_release.sh --platform android_arm64"
        exit 1
    fi
    require_host_file "${ATLAS_SDK}/lib/libatlas.so" "libatlas.so"
    require_host_file "${ATLAS_SDK}/include/atlas/atlas_runtime.h" "SDK headers"

    # Verify cross-compiler exists
    if ! command -v "${CROSS_COMPILE}clang++" &>/dev/null; then
        echo "ERROR: Cross-compiler not found: ${CROSS_COMPILE}clang++"
        echo "  Install Android NDK and set CROSS_COMPILE, or ensure NDK is in PATH."
        echo "  Example: export CROSS_COMPILE=\$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-"
        exit 1
    fi

    export ATLAS_SDK
    export CROSS_COMPILE

    (cd "${SAMPLE_DIR}" && make clean && make)

    local binary_path="${SAMPLE_DIR}/build/atlas_snpe_cpu_test"
    require_host_file "${binary_path}" "Android binary"

    echo "Build successful: ${binary_path}"
    file "${binary_path}"
}

# ============================================================================
# push  — Push resources to Android device
# ============================================================================
cmd_push() {
    echo "=== Pushing to Android device ==="

    local binary_path="${SAMPLE_DIR}/build/atlas_snpe_cpu_test"

    require_host_file "${binary_path}" "Android binary"
    require_host_file "${MANIFEST_SRC}" "manifest.json"
    require_host_file "${ONNX_SRC}" "relu.onnx"
    require_host_file "${DLC_SRC}" "relu_snpe.dlc"
    require_host_file "${ATLAS_SDK}/lib/libatlas.so" "libatlas.so"
    require_host_file "${ATLAS_SDK}/lib/libonnxruntime.so" "libonnxruntime.so"

    # Ensure device is connected
    adb_exec get-state >/dev/null

    # Create directory on device
    adb_exec shell "rm -rf '${DEVICE_APP_DIR}'"
    adb_exec shell "mkdir -p '${DEVICE_APP_DIR}'"

    # Push binary and manifest
    adb_exec push "${binary_path}" "${DEVICE_APP_DIR}/atlas_snpe_cpu_test"
    adb_exec push "${MANIFEST_SRC}" "${DEVICE_APP_DIR}/manifest.json"
    adb_exec push "${ONNX_SRC}" "${DEVICE_APP_DIR}/relu.onnx"

    # Push DLC model
    adb_exec push "${DLC_SRC}" "${DEVICE_APP_DIR}/relu_snpe.dlc"

    # Push shared libraries to system lib directory (requires root)
    adb_exec root >/dev/null
    adb_exec remount >/dev/null 2>&1 || true
    adb_exec push "${ATLAS_SDK}/lib/libatlas.so" "${DEVICE_LIB_DIR}/libatlas.so"
    adb_exec push "${ATLAS_SDK}/lib/libonnxruntime.so" "${DEVICE_LIB_DIR}/libonnxruntime.so"

    echo "Push complete."
    echo "  Binary:   ${DEVICE_APP_DIR}/atlas_snpe_cpu_test"
    echo "  Manifest: ${DEVICE_APP_DIR}/manifest.json"
    echo "  ONNX:     ${DEVICE_APP_DIR}/relu.onnx"
    echo "  DLC:      ${DEVICE_APP_DIR}/relu_snpe.dlc"
    echo "  Libs:     ${DEVICE_LIB_DIR}/libatlas.so, libonnxruntime.so"
}

# ============================================================================
# run  — Run on Android device
# ============================================================================
cmd_run() {
    echo "=== Running on Android device ==="

    adb_exec shell "cd '${DEVICE_APP_DIR}' && chmod 755 ./atlas_snpe_cpu_test && ./atlas_snpe_cpu_test ./manifest.json"
}

# ============================================================================
# all  — Full pipeline: gen → build → push → run
# ============================================================================
cmd_all() {
    cmd_gen
    cmd_build
    cmd_push
    cmd_run
}

# ============================================================================
# Main
# ============================================================================
case "${1:-}" in
    env)
        cmd_env
        ;;
    gen)
        cmd_gen
        ;;
    build)
        cmd_build
        ;;
    push)
        cmd_push
        ;;
    run)
        cmd_run
        ;;
    all)
        cmd_all
        ;;
    *)
        help
        exit 1
        ;;
esac