cc_test(
    name = "manifest_parser_test",
    srcs = ["manifest_parser_test.cc"],
    data = glob(["test_data/**"]),
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/core:manifest_config",
        "//src/core:manifest_parser",
        "//src/utils:types",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "model_manager_test",
    srcs = ["model_manager_test.cc"],
    data = ["//tests/backend/cpu:test_models"],
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/cpu:cpu_backend",
        "//src/backend/cpu:cpu_backend_context",
        "//src/core:manifest_config",
        "//src/core:model_manager",
        "//src/utils:types",
        "@googletest//:gtest_main",
    ],
)
