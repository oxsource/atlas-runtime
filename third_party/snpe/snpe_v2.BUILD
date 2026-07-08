# SNPE SDK 2.x BUILD template for @snpe_sdk.
# Verified with: SNPE 2.21.0.240401
#   Include layout: include/SNPE/SNPE/SNPE.hpp
#   Note: 2.x has both .h and .hpp headers — glob both.
#   Linux aarch64 .so: lib/aarch64-oe-linux-gcc8.2/libSNPE.so
#   Android arm64 .so: lib/aarch64-android/libSNPE.so

load("@oxsource_atlas//platforms:platforms.bzl", "atlas_select")

cc_library(
    name = "snpe",
    hdrs = glob([
        "snpe_sdk_root/include/SNPE/**/*.hpp",
        "snpe_sdk_root/include/SNPE/**/*.h",
    ]),
    includes = ["snpe_sdk_root/include/SNPE"],
    srcs = atlas_select(
        linux_aarch64 = glob(["snpe_sdk_root/lib/aarch64-oe-linux-gcc8.2/libSNPE.so"]),
        linux_x86_64  = glob(["snpe_sdk_root/lib/x86_64-linux-clang/libSNPE.so"]),
        android_arm64 = glob(["snpe_sdk_root/lib/aarch64-android/libSNPE.so"]),
        default       = [],
    ),
    visibility = ["//visibility:public"],
)