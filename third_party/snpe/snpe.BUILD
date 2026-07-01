# SNPE SDK Bazel BUILD file
# build_file for @snpe_sdk — paths are relative to the symlinked root.
#
# Setup:
#   1. Set SNPE_SDK_PATH environment variable to the SNPE SDK installation directory.
#   2. Build with: SNPE_SDK_PATH=/path/to/snpe-sdk bazel build //...
#
# Verified with: SNPE SDK 2.21.0.240401
# Layout:
#   <SNPE_SDK_PATH>/
#     include/SNPE/SNPE/SNPE.hpp
#     include/SNPE/DlSystem/DlSystem.hpp
#     lib/
#       aarch64-oe-linux-gcc8.2/libSNPE.so   (Linux aarch64, OE toolchain)
#       aarch64-android/libSNPE.so            (Android arm64)

cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/SNPE/**/*.hpp"]),
    includes = ["snpe_sdk_root/include/SNPE"],
    srcs = select({
        "//platforms:linux_aarch64": glob(
            ["snpe_sdk_root/lib/aarch64-oe-linux-gcc8.2/*.so"],
        ),
        "//platforms:android_arm64": glob(
            ["snpe_sdk_root/lib/aarch64-android/*.so"],
        ),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
