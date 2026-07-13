# Public library target that aggregates all Atlas modules into a single
# shared or static library for external consumption.
#
# External Bazel projects depend on //src/public:atlas.
# Internal targets (tests, examples) continue to depend on granular targets
# under src/api, src/core, etc.

load("//third_party/onnxruntime:defs.bzl", "onnxruntime_deps")
load("//third_party/snpe:defs.bzl", "snpe_sdk_deps")

cc_library(
    name = "types",
    hdrs = glob(["include/atlas/*.h"]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)



cc_library(
    name = "atlas",
    hdrs = glob(["include/atlas/*.h"]),
    strip_include_prefix = "include",
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-DATLAS_SHARED_LIBRARY",
    ],
    alwayslink = 1,
    deps = [
        "//src/api:atlas_runtime",
        "//src/api:model_handle",
        "//src/backend/cpu:cpu_backend",
        "//src/backend/cpu:cpu_backend_context",
        "//src/backend/snpe:snpe_backend",
        "//src/backend/snpe:snpe_backend_context",
        "//src/core:manifest_config",
        "//src/core:manifest_parser",
        "//src/core:model_manager",
        "//src/pipeline:pipeline",
        "//src/utils:logger",
        "//src/utils:types",
        "//src/utils:version",
    ] + onnxruntime_deps() + snpe_sdk_deps(),
    visibility = ["//visibility:public"],
)

# Self-contained shared library for external (non-Bazel) consumers.
# MediaPipe-style: cc_binary(linkshared=True, linkstatic=True) +
# ATLAS_API-decorated public classes ensure symbols are exported.
cc_binary(
    name = "atlas_shared",
    srcs = ["atlas_init.cc"],
    linkshared = True,
    linkstatic = True,
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-DATLAS_SHARED_LIBRARY",
    ],
    deps = [":atlas"],
    visibility = ["//visibility:public"],
)