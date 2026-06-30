# Public library target that aggregates all Atlas modules into a single
# shared or static library for external consumption.
#
# External Bazel projects depend on //src/public:atlas.
# Internal targets (tests, examples) continue to depend on granular targets
# under src/api, src/core, etc.

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
        "//src/utils:types",
        "//src/utils:version",
    ] + select({
        "//platforms:macos_arm64":    ["@onnxruntime_macos_arm64//:onnxruntime"],
        "//platforms:macos_x86_64":   ["@onnxruntime_macos_arm64//:onnxruntime"],
        "//platforms:linux_aarch64":  ["@onnxruntime_linux_aarch64//:onnxruntime"],
        "//platforms:android_arm64":  ["@onnxruntime_android_arm64//:onnxruntime"],
        "//platforms:android_x86_64": ["@onnxruntime_android_x86_64//:onnxruntime"],
        "//conditions:default":       ["@onnxruntime_linux_x86_64//:onnxruntime"],
    }),
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