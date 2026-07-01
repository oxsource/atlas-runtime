cc_test(
    name = "snpe_backend_test",
    srcs = ["snpe_backend_test.cc"],
    data = glob(["test_data/**"]),
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/snpe:snpe_backend",
        "//src/backend/snpe:snpe_backend_context",
        "//src/core:manifest_config",
        "//src/utils:types",
        "@googletest//:gtest_main",
    ],
    visibility = ["//visibility:private"],
)
