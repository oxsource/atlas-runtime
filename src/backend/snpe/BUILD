# SNPE backend — version controlled via snpe_major in WORKSPACE.
#
# Usage:
#   export SNPE_SDK_PATH=/path/to/snpe-sdk
#   bazel build --config=linux_aarch64 //src/backend/snpe/...
#
# Version is declared in WORKSPACE; no --define or --config=snpe_vN needed.

load("@snpe_sdk//:version.bzl", "SNPE_MAJOR")

# SNPE_ENABLED is defined only on target platforms (linux_aarch64 / android_arm64).
SNPE_PLATFORM = select({
    "//platforms:linux_aarch64": ["ATLAS_SNPE_ENABLED=1"],
    "//platforms:android_arm64": ["ATLAS_SNPE_ENABLED=1"],
    "//conditions:default": [],
})

# SDK dependency — only linked on target platforms.
SNPE_SDK_DEP = select({
    "//platforms:linux_aarch64": ["@snpe_sdk//:snpe"],
    "//platforms:android_arm64": ["@snpe_sdk//:snpe"],
    "//conditions:default": [],
})

# Version-specific source files: _v1.cc / _v2.cc when SDK is available,
# _stub.cc otherwise (no SDK or non-target platform).
_BACKEND_V = (
    ["snpe_backend_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR
    else ["snpe_backend_stub.cc"]
)
_CONTEXT_V = (
    ["snpe_backend_context_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR
    else ["snpe_backend_context_stub.cc"]
)

SNPE_BACKEND_SRC = select({
    "//platforms:linux_aarch64": _BACKEND_V,
    "//platforms:android_arm64": _BACKEND_V,
    "//conditions:default": ["snpe_backend_stub.cc"],
})

SNPE_CONTEXT_SRC = select({
    "//platforms:linux_aarch64": _CONTEXT_V,
    "//platforms:android_arm64": _CONTEXT_V,
    "//conditions:default": ["snpe_backend_context_stub.cc"],
})

cc_library(
    name = "snpe_backend_context",
    srcs = SNPE_CONTEXT_SRC,
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
    srcs = SNPE_BACKEND_SRC,
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