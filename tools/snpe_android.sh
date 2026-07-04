#!/bin/bash
# Usage:
#   source tools/snpe_android.sh env    # setup SNPE/Android environment variables
#   bash tools/snpe_android.sh gen      # generate sample DLC models on host
#   bash tools/snpe_android.sh build    # build the Android snpe_cpu binary
#   bash tools/snpe_android.sh push-bin # build and push only the snpe_cpu binary
#   bash tools/snpe_android.sh push     # build and push binary + deps to device
#   bash tools/snpe_android.sh run      # build, push, and run on Android device

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

TARGET="//examples/snpe_cpu:snpe_cpu"
BAZEL_CONFIG="${BAZEL_CONFIG:-android_arm64}"
ADB_BIN="${ADB_BIN:-adb}"
ADB_SERIAL="${ADB_SERIAL:-}"
HOST_MODEL_DIR="${HOST_MODEL_DIR:-/tmp/snpe_sample_models_v1}"
DEVICE_MODEL_DIR="${DEVICE_MODEL_DIR:-/data/local/tmp/snpe_sample_models_v1}"
DEVICE_APP_DIR="${DEVICE_APP_DIR:-/data/local/tmp/snpe_cpu}"
DEVICE_LIB_DIR="${DEVICE_LIB_DIR:-/system/lib64}"
MANIFEST_SRC="${REPO_ROOT}/examples/snpe_cpu/manifest.json"
ATLAS_SDK_ONNXRUNTIME_LIB="${REPO_ROOT}/atlas-sdk/android-arm64/lib/libonnxruntime.so"

usage() {
    echo "Usage: $0 {env|gen|build|push-bin|push|run}"
    echo "  env    - export SNPE and Android build variables"
    echo "  gen    - generate sample DLC models on host"
    echo "  build  - bazel build --config=android_arm64 //examples/snpe_cpu:snpe_cpu"
    echo "  push-bin - build and push only the snpe_cpu binary"
    echo "  push   - build and push binary; keep the device's existing libSNPE.so"
    echo "  run    - build, push, and execute on device"
}

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

require_env() {
    if [[ -z "${SNPE_SDK_PATH:-}" ]]; then
        echo "ERROR: SNPE_SDK_PATH is not set."
        exit 1
    fi
    if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
        echo "ERROR: ANDROID_NDK_HOME is not set."
        exit 1
    fi
}

build_android_binary() {
    require_env
    (cd "${REPO_ROOT}" && bazel build --config="${BAZEL_CONFIG}" "${TARGET}")
}

generate_models() {
    (cd "${REPO_ROOT}" && python3 examples/snpe_cpu/gen_models.py "${HOST_MODEL_DIR}")
}

prepare_system_lib_dir() {
    echo "Preparing ${DEVICE_LIB_DIR} (adb root + remount)..."
    adb_exec root >/dev/null
    adb_exec remount >/dev/null
}

ensure_device() {
    adb_exec get-state >/dev/null
}

push_binary_only() {
    build_android_binary

    local binary_path="${REPO_ROOT}/bazel-bin/examples/snpe_cpu/snpe_cpu"

    require_host_file "${binary_path}" "Android binary"

    ensure_device
    adb_exec shell "mkdir -p '${DEVICE_APP_DIR}'"
    adb_exec push "${binary_path}" "${DEVICE_APP_DIR}/snpe_cpu"
}

push_to_device() {
    build_android_binary
    generate_models

    local binary_path="${REPO_ROOT}/bazel-bin/examples/snpe_cpu/snpe_cpu"

    require_host_file "${binary_path}" "Android binary"
    require_host_file "${ATLAS_SDK_ONNXRUNTIME_LIB}" "atlas-sdk libonnxruntime.so"
    require_host_file "${HOST_MODEL_DIR}/relu_snpe.dlc" "sample DLC model"
    require_host_file "${MANIFEST_SRC}" "manifest"

    ensure_device
    prepare_system_lib_dir

    adb_exec shell "rm -rf '${DEVICE_APP_DIR}' '${DEVICE_MODEL_DIR}' && mkdir -p '${DEVICE_APP_DIR}' '${DEVICE_MODEL_DIR}'"
    adb_exec push "${binary_path}" "${DEVICE_APP_DIR}/snpe_cpu"
    adb_exec push "${ATLAS_SDK_ONNXRUNTIME_LIB}" "${DEVICE_LIB_DIR}/libonnxruntime.so"
    adb_exec push "${MANIFEST_SRC}" "${DEVICE_APP_DIR}/manifest.json"
    adb_exec push "${HOST_MODEL_DIR}/relu_snpe.dlc" "${DEVICE_MODEL_DIR}/relu_snpe.dlc"
}

run_on_device() {
    adb_exec shell "cd '${DEVICE_APP_DIR}' && export SAMPLE_MODEL_DIR='${DEVICE_MODEL_DIR}' && chmod 755 ./snpe_cpu && ./snpe_cpu ./manifest.json"
}

case "${1:-}" in
    env)
        export SNPE_ROOT="${SNPE_ROOT:-/opt/qcom/sdk/snpe-1.50.0.2622}"
        export SNPE_SDK_PATH="${SNPE_SDK_PATH:-${SNPE_ROOT}}"
        export PYTHONPATH="${SNPE_SDK_PATH}/lib/python${PYTHONPATH:+:${PYTHONPATH}}"
        echo "SNPE/Android environment variables set."
        echo "  SNPE_SDK_PATH=${SNPE_SDK_PATH}"
        echo "  ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-<not set>}"
        echo "  ADB_SERIAL=${ADB_SERIAL:-<not set>}"
        echo "  DEVICE_LIB_DIR=${DEVICE_LIB_DIR}"
        ;;
    gen)
        generate_models
        ;;
    build)
        build_android_binary
        ;;
    push-bin)
        push_binary_only
        ;;
    push)
        push_to_device
        ;;
    run)
        run_on_device
        ;;
    *)
        usage
        exit 1
        ;;
esac