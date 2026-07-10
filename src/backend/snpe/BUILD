# SNPE backend — version controlled via snpe_major in WORKSPACE.
#
# Usage:
#   export SNPE_SDK_PATH=/path/to/snpe-sdk
#   bazel build --config=linux_aarch64 //src/backend/snpe/...
#
# Version is declared in WORKSPACE; no --define or --config=snpe_vN needed.

load(
    "//third_party/snpe:defs.bzl",
    "snpe_backend_srcs",
    "snpe_context_srcs",
    "snpe_platform_defines",
    "snpe_sdk_deps",
)

cc_library(
    name = "snpe_backend_context",
    srcs = snpe_context_srcs(),
    hdrs = ["snpe_backend_context.h"],
    defines = snpe_platform_defines(),
    deps = [
        ":snpe_aligned_buffer",
        ":snpe_memory_pool",
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend_context",
        "//src/utils:logger",
        "//src/utils:types",
    ] + snpe_sdk_deps(),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "snpe_aligned_buffer",
    hdrs = ["snpe_aligned_buffer.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "snpe_memory_pool",
    hdrs = ["snpe_memory_pool.h"],
    deps = [":snpe_aligned_buffer"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "snpe_backend",
    srcs = snpe_backend_srcs(),
    hdrs = ["snpe_backend.h"],
    defines = snpe_platform_defines(),
    deps = [
        ":snpe_aligned_buffer",
        ":snpe_backend_context",
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/core:manifest_config",
        "//src/utils:logger",
        "//src/utils:types",
    ] + snpe_sdk_deps(),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)