#!/bin/bash
# Usage:
#   source tools/android_log.sh env    # setup Android build variables
#   bash tools/android_log.sh build    # build the Android android_log binary
#   bash tools/android_log.sh push     # build and push binary to device
#   bash tools/android_log.sh run      # build, push, run, and dump logcat

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

TARGET="//examples/android_log:android_log"
BAZEL_CONFIG="${BAZEL_CONFIG:-android_arm64}"
ADB_BIN="${ADB_BIN:-adb}"
ADB_SERIAL="${ADB_SERIAL:-}"
DEVICE_DIR="${DEVICE_DIR:-/data/local/tmp/android_log}"

usage() {
    echo "Usage: $0 {env|build|push|run}"
    echo "  env    - export Android build variables"
    echo "  build  - bazel build --config=android_arm64 //examples/android_log:android_log"
    echo "  push   - build and push the binary to device"
    echo "  run    - build, push, run, and print logcat output"
}

adb_exec() {
    if [[ -n "${ADB_SERIAL}" ]]; then
        "${ADB_BIN}" -s "${ADB_SERIAL}" "$@"
    else
        "${ADB_BIN}" "$@"
    fi
}

require_env() {
    if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
        echo "ERROR: ANDROID_NDK_HOME is not set."
        exit 1
    fi
}

build_binary() {
    require_env
    (cd "${REPO_ROOT}" && bazel build --config="${BAZEL_CONFIG}" "${TARGET}")
}

ensure_device() {
    adb_exec get-state >/dev/null
}

push_binary() {
    build_binary

    local binary_path="${REPO_ROOT}/bazel-bin/examples/android_log/android_log"
    if [[ ! -f "${binary_path}" ]]; then
        echo "ERROR: Android binary not found: ${binary_path}"
        exit 1
    fi

    ensure_device
    adb_exec shell "rm -rf '${DEVICE_DIR}' && mkdir -p '${DEVICE_DIR}'"
    adb_exec push "${binary_path}" "${DEVICE_DIR}/android_log"
    adb_exec shell "chmod 755 '${DEVICE_DIR}/android_log'"
}

run_binary() {
    push_binary

    adb_exec logcat -c
    adb_exec shell "cd '${DEVICE_DIR}' && ./android_log"
    adb_exec logcat -d -s android_log
}

case "${1:-}" in
    env)
        echo "Android environment variables set."
        echo "  ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-<not set>}"
        echo "  ADB_SERIAL=${ADB_SERIAL:-<not set>}"
        ;;
    build)
        build_binary
        ;;
    push)
        push_binary
        ;;
    run)
        run_binary
        ;;
    *)
        usage
        exit 1
        ;;
esac