cc_library(
    name = "types",
    hdrs = ["types.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "logger",
    srcs = ["logger.cc"],
    hdrs = ["logger.h"],
    visibility = ["//visibility:public"],
    linkopts = select({
        "@platforms//os:android": ["-llog"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "version",
    srcs = ["version.cc"],
    hdrs = ["version.h"],
    visibility = ["//visibility:public"],
)
