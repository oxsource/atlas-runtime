cc_library(
    name = "manifest_config",
    hdrs = ["manifest_config.h"],
    deps = ["//src/utils:types"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "manifest_parser",
    srcs = ["manifest_parser.cc"],
    hdrs = ["manifest_parser.h"],
    deps = [
        ":manifest_config",
        "//src/utils:types",
        "@nlohmann_json//:json",
    ],
    visibility = ["//visibility:public"],
)
