# SNPE backend — conditionally compiled.
# Linux aarch64 / Android arm64: ATLAS_SNPE_ENABLED defined, links @snpe_sdk.
# All other platforms (macOS / Linux x86_64): stub compiles, no SDK dependency.

SNPE_PLATFORM = select({
    "//platforms:linux_aarch64": ["ATLAS_SNPE_ENABLED=1"],
    "//platforms:android_arm64": ["ATLAS_SNPE_ENABLED=1"],
    "//conditions:default": [],
})

SNPE_SDK_DEP = select({
    "//platforms:linux_aarch64": ["@snpe_sdk//:snpe"],
    "//platforms:android_arm64": ["@snpe_sdk//:snpe"],
    "//conditions:default": [],
})

cc_library(
    name = "snpe_backend_context",
    srcs = ["snpe_backend_context.cc"],
    hdrs = ["snpe_backend_context.h"],
    defines = SNPE_PLATFORM,
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend_context",
        "//src/utils:types",
    ] + SNPE_SDK_DEP,
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "snpe_backend",
    srcs = ["snpe_backend.cc"],
    hdrs = ["snpe_backend.h"],
    defines = SNPE_PLATFORM,
    deps = [
        ":snpe_backend_context",
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/core:manifest_config",
        "//src/utils:types",
    ] + SNPE_SDK_DEP,
    alwayslink = 1,
    visibility = ["//visibility:public"],
)