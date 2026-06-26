cc_test(
    name = "pipeline_test",
    srcs = ["pipeline_test.cc"],
    deps = [
        "//src/pipeline:pipeline",
        "//src/pipeline:nodes",
        "//src/utils:types",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "pipeline_manifest_test",
    srcs = ["pipeline_manifest_test.cc"],
    deps = [
        "//src/core:manifest_config",
        "//src/core:manifest_parser",
        "//src/pipeline:pipeline",
        "//src/pipeline:pipeline_node_factory",
        "//src/pipeline:nodes",
        "//src/utils:types",
        "@nlohmann_json//:json",
        "@googletest//:gtest_main",
    ],
)
