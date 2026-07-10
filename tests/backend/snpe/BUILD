cc_test(
    name = "snpe_memory_pool_test",
    srcs = ["snpe_memory_pool_test.cc"],
    deps = [
        "//src/backend/snpe:snpe_memory_pool",
        "@googletest//:gtest_main",
    ],
    visibility = ["//visibility:private"],
)

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
