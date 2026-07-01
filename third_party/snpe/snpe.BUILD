# SNPE SDK Bazel BUILD file
# build_file for @snpe_sdk — paths are relative to the symlinked root.
#
# Setup:
#   1. Set SNPE_SDK_PATH environment variable to the SNPE SDK installation directory.
#   2. Build with: SNPE_SDK_PATH=/path/to/snpe-sdk bazel build //...
#
# Verified with:
#   - SNPE SDK 2.21.0.240401  (ATLAS_SNPE_VERSION_MAJOR=2)
#   - SNPE SDK 1.50.0.2622     (ATLAS_SNPE_VERSION_MAJOR=1)
#
# === SNPE SDK version-specific directory layouts ===
#
# SNPE 2.x (e.g. 2.21.0):
#   <SNPE_SDK_PATH>/
#     include/SNPE/
#       SNPE/SNPE.hpp
#       DlSystem/DlSystem.hpp
#       DlContainer/IDlContainer.hpp
#     lib/
#       aarch64-oe-linux-gcc8.2/libSNPE.so   (Linux aarch64, OE toolchain)
#       aarch64-android/libSNPE.so            (Android arm64)
#
# SNPE 1.x (e.g. 1.50.0):
#   <SNPE_SDK_PATH>/
#     include/zdl/          <-- 1.x wraps headers under zdl/ subdirectory
#       SNPE/SNPE.hpp
#       DlSystem/DlSystem.hpp
#       DlContainer/IDlContainer.hpp
#     lib/
#       aarch64-linux-gcc4.9/libSNPE.so       (Linux aarch64, compiler version suffix)
#       aarch64-android-clang6.0/libSNPE.so   (Android arm64, compiler version suffix)
#       aarch64-oe-linux-gcc8.2/libSNPE.so    (OE Linux)
#
# When switching between versions, verify the glob patterns below match
# the actual SDK layout. The hdrs glob uses "snpe_sdk_root/include/**"
# to capture both layout styles (2.x flat + 1.x zdl/ nesting).

cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/**/*.hpp"]),
    includes = select({
        "//platforms:linux_aarch64": [
            "snpe_sdk_root/include/SNPE",     # 2.x: include/SNPE/<rest>
            "snpe_sdk_root/include/zdl",      # 1.x: include/zdl/<rest>
        ],
        "//platforms:android_arm64": [
            "snpe_sdk_root/include/SNPE",     # 2.x: include/SNPE/<rest>
            "snpe_sdk_root/include/zdl",      # 1.x: include/zdl/<rest>
        ],
        "//conditions:default": [
            "snpe_sdk_root/include/SNPE",
            "snpe_sdk_root/include/zdl",
        ],
    }),
    srcs = select({
        "//platforms:linux_aarch64": glob(
            [
                "snpe_sdk_root/lib/aarch64-oe-linux-gcc8.2/*.so",    # 2.x
                "snpe_sdk_root/lib/aarch64-linux-gcc*/*.so",          # 1.x (gcc4.9 etc.)
            ],
        ),
        "//platforms:android_arm64": glob(
            [
                "snpe_sdk_root/lib/aarch64-android/*.so",             # 2.x
                "snpe_sdk_root/lib/aarch64-android-clang*/*.so",      # 1.x (clang6.0 etc.)
            ],
        ),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)