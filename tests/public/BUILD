cc_test(
    name = "atlas_lib_test",
    srcs = ["atlas_lib_test.cc"],
    data = ["//tests/backend/cpu:test_models"],
    deps = [
        "//src/public:atlas",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "atlas_export_test",
    srcs = ["atlas_export_test.cc"],
    deps = [
        "//src/public:atlas",
        "@googletest//:gtest_main",
    ],
)
