cc_library(
    name = "i_backend",
    hdrs = ["i_backend.h"],
    deps = [
        "//src/core:manifest_config",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "backend_factory",
    srcs = ["backend_factory.cc"],
    hdrs = ["backend_factory.h"],
    deps = [":i_backend"],
    visibility = ["//visibility:public"],
)
