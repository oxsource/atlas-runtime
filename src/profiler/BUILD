cc_library(
    name = "profile_config",
    hdrs = ["profile_config.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "profiler",
    srcs = ["profiler.cc"],
    hdrs = ["profiler.h"],
    deps = [
        ":profile_config",
        "//src/utils:logger",
    ],
    visibility = ["//visibility:public"],
)
