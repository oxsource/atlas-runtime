# SNPE SDK Bazel BUILD file
# build_file for @snpe_sdk — paths are relative to the symlinked root.
#
# Setup:
#   1. Set SNPE_SDK_PATH environment variable to the SNPE SDK installation directory.
#   2. Build with: SNPE_SDK_PATH=/path/to/snpe-sdk bazel build //...
#
# Note: .so glob patterns below assume the typical SNPE SDK layout.
# Adjust subdirectory names (aarch64-linux-gcc / aarch64-android-clang) to match
# the actual SDK version you have installed.
#
# Layout assumption:
#   <SNPE_SDK_PATH>/
#     include/zdl/SNPE/SNPE.hpp
#     include/zdl/DlSystem/DlSystem.hpp
#     lib/
#       aarch64-linux-gcc/libSNPE.so
#       aarch64-android-clang/libSNPE.so

cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/zdl/**/*.hpp"]),
    includes = ["snpe_sdk_root/include"],
    srcs = select({
        "//platforms:linux_aarch64": glob(
            ["snpe_sdk_root/lib/aarch64-linux-gcc/*.so"],
        ),
        "//platforms:android_arm64": glob(
            ["snpe_sdk_root/lib/aarch64-android-clang/*.so"],
        ),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)