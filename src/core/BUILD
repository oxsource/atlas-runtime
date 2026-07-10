cc_library(
    name = "manifest_config",
    hdrs = ["manifest_config.h"],
    deps = [
        "//src/profiler:profile_config",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "manifest_parser",
    srcs = ["manifest_parser.cc"],
    hdrs = ["manifest_parser.h"],
    deps = [
        ":manifest_config",
        "//src/utils:logger",
        "//src/utils:types",
        "@nlohmann_json//:json",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "model_manager",
    srcs = ["model_manager.cc"],
    hdrs = ["model_manager.h"],
    deps = [
        ":manifest_config",
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/backend/base:i_backend_context",
        "//src/backend/base:profiling_backend",
        "//src/pipeline:pipeline",
        "//src/profiler:profiler",
        "//src/utils:logger",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)
