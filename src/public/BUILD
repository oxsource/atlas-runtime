# Public library target that aggregates all Atlas modules into a single
# shared or static library for external consumption.
#
# External Bazel projects depend on //src/public:atlas.
# Internal targets (tests, examples) continue to depend on granular targets
# under src/api, src/core, etc.

cc_library(
    name = "atlas",
    srcs = [
        "atlas_init.cc",
    ],
    hdrs = glob(["include/atlas/*.h"]),
    # Public headers are exposed under the "atlas/" include prefix.
    # src/public/include is the include root; consumers write
    #   #include "atlas/atlas_runtime.h"
    strip_include_prefix = "include",
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-DATLAS_SHARED_LIBRARY",
    ],
    # SONAME / install_name is intentionally NOT set here.
    # Bazel manages RPATH for sandbox builds; setting a custom install_name
    # breaks the sandbox's dynamic linker resolution.
    # The SONAME for distribution is applied by the install script
    # (tools/install_atlas.sh) when creating the release package.
    # alwayslink ensures atlas_init.cc (backend anchor) is not stripped
    # by the linker when building shared libraries.
    alwayslink = 1,
    # Aggregate all implementation modules. alwayslink on cpu_backend /
    # cpu_backend_context ensures backend registration static initializers
    # are retained.
    deps = [
        "//src/api:atlas_runtime",
        "//src/api:model_handle",
        "//src/backend/cpu:cpu_backend",
        "//src/backend/cpu:cpu_backend_context",
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
