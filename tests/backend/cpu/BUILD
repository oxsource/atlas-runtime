cc_test(
    name = "cpu_backend_test",
    srcs = ["cpu_backend_test.cc"],
    data = glob(["test_data/**"]),
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/cpu:cpu_backend",
        "//src/core:manifest_config",
        "//src/utils:types",
        "@googletest//:gtest_main",
    ],
)
