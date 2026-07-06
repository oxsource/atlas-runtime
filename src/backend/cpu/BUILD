load("//third_party/onnxruntime:defs.bzl", "onnxruntime_deps")

cc_library(
    name = "cpu_backend_context",
    srcs = ["cpu_backend_context.cc"],
    hdrs = ["cpu_backend_context.h"],
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend_context",
    ] + onnxruntime_deps(),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "cpu_backend",
    srcs = ["cpu_backend.cc"],
    hdrs = ["cpu_backend.h"],
    deps = [
        ":cpu_backend_context",
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/utils:logger",
        "//src/utils:strings",
        "//src/utils:types",
    ] + onnxruntime_deps(),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)
