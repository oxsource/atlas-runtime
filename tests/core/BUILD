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
