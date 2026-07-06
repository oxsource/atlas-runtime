cc_library(
    name = "pipeline_node",
    hdrs = ["pipeline_node.h"],
    deps = ["//src/utils:types"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "pipeline_node_factory",
    srcs = ["pipeline_node_factory.cc"],
    hdrs = ["pipeline_node_factory.h"],
    deps = [
        ":pipeline_node",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "nodes",
    deps = [
        "//src/pipeline/nodes:bgr_to_rgb_node",
        "//src/pipeline/nodes:chw_to_hwc_node",
        "//src/pipeline/nodes:dtype_convert_node",
        "//src/pipeline/nodes:hwc_to_chw_node",
        "//src/pipeline/nodes:normalize_node",
        "//src/pipeline/nodes:resize_node",
        "//src/pipeline/nodes:rgb_to_bgr_node",
        "//src/pipeline/nodes:softmax_node",
        "//src/pipeline/nodes:topk_node",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "pipeline",
    srcs = ["pipeline.cc"],
    hdrs = ["pipeline.h"],
    deps = [
        ":nodes",
        ":pipeline_node",
        ":pipeline_node_factory",
        "//src/core:manifest_config",
        "//src/utils:types",
    ],
    visibility = ["//visibility:public"],
)
