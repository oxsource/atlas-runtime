cc_library(
    name = "cpu_backend",
    srcs = ["cpu_backend.cc"],
    hdrs = ["cpu_backend.h"],
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/utils:types",
    ] + select({
        "@bazel_tools//src/conditions:darwin_arm64": ["@onnxruntime_macos_arm64//:onnxruntime"],
        "//conditions:default":                      ["@onnxruntime_linux_x86_64//:onnxruntime"],
    }),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)
