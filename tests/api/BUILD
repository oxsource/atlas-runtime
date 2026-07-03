cc_test(
    name = "atlas_runtime_test",
    srcs = ["atlas_runtime_test.cc"],
    data = ["//tests/backend/cpu:test_models"],
    deps = [
        "//src/api:atlas_runtime",
        "//src/api:model_handle",
        "//src/backend/cpu:cpu_backend",
        "//src/backend/cpu:cpu_backend_context",
        "//src/backend/snpe:snpe_backend",
        "//src/backend/snpe:snpe_backend_context",
        "//src/utils:types",
        "@googletest//:gtest_main",
    ],
)
