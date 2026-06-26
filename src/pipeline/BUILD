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
    srcs = [
        "nodes/bgr_to_rgb_node.cc",
        "nodes/chw_to_hwc_node.cc",
        "nodes/dtype_convert_node.cc",
        "nodes/hwc_to_chw_node.cc",
        "nodes/normalize_node.cc",
        "nodes/resize_node.cc",
        "nodes/rgb_to_bgr_node.cc",
        "nodes/softmax_node.cc",
        "nodes/topk_node.cc",
    ],
    hdrs = [
        "nodes/bgr_to_rgb_node.h",
        "nodes/chw_to_hwc_node.h",
        "nodes/dtype_convert_node.h",
        "nodes/hwc_to_chw_node.h",
        "nodes/normalize_node.h",
        "nodes/resize_node.h",
        "nodes/rgb_to_bgr_node.h",
        "nodes/softmax_node.h",
        "nodes/topk_node.h",
    ],
    deps = [
        ":pipeline_node",
        ":pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
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
