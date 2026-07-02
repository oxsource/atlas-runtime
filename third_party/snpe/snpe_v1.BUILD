# SNPE SDK 1.x BUILD template for @snpe_sdk.
# Verified with: SNPE 1.50.0.2622
#   Include layout: include/zdl/SNPE/SNPE.hpp
#   Linux aarch64 .so: lib/aarch64-linux-gcc4.9/libSNPE.so
#   Android arm64 .so: lib/aarch64-android-clang6.0/libSNPE.so

cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/zdl/**/*.hpp"]),
    includes = ["snpe_sdk_root/include/zdl"],
    srcs = select({
        "@//platforms:linux_aarch64": glob([
            "snpe_sdk_root/lib/aarch64-linux-gcc4.9/*.so",
        ]),
        "@//platforms:android_arm64": glob([
            "snpe_sdk_root/lib/aarch64-android-clang6.0/*.so",
        ]),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)