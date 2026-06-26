cc_library(
    name = "model_handle",
    srcs = ["model_handle.cc"],
    hdrs = ["model_handle.h"],
    deps = [
        "//src/core:model_manager",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "atlas_runtime",
    srcs = ["atlas_runtime.cc"],
    hdrs = ["atlas_runtime.h"],
    deps = [
        ":model_handle",
        "//src/core:manifest_config",
        "//src/core:manifest_parser",
        "//src/core:model_manager",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)
