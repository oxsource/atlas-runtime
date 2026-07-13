cc_library(
    name = "i_backend_context",
    hdrs = ["i_backend_context.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "i_backend",
    hdrs = ["i_backend.h"],
    deps = [
        ":i_backend_context",
        "//src/core:manifest_config",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "backend_factory",
    srcs = ["backend_factory.cc"],
    hdrs = ["backend_factory.h"],
    deps = [
        ":i_backend",
        ":i_backend_context",
        "//src/utils:logger",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "profiling_backend",
    srcs = ["profiling_backend.cc"],
    hdrs = ["profiling_backend.h"],
    deps = [
        ":i_backend",
        "//src/profiler:profiler",
    ],
    visibility = ["//visibility:public"],
)
