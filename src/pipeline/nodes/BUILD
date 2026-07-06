# Individual pipeline node targets.
# Each node compiles its own translation unit as a separate cc_library
# with alwayslink=1 to ensure static initializers (ATLAS_REGISTER_PIPELINE_NODE)
# are linked into every binary that depends on it.

cc_library(
    name = "bgr_to_rgb_node",
    srcs = ["bgr_to_rgb_node.cc"],
    hdrs = ["bgr_to_rgb_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "chw_to_hwc_node",
    srcs = ["chw_to_hwc_node.cc"],
    hdrs = ["chw_to_hwc_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "dtype_convert_node",
    srcs = ["dtype_convert_node.cc"],
    hdrs = ["dtype_convert_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "hwc_to_chw_node",
    srcs = ["hwc_to_chw_node.cc"],
    hdrs = ["hwc_to_chw_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "normalize_node",
    srcs = ["normalize_node.cc"],
    hdrs = ["normalize_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "resize_node",
    srcs = ["resize_node.cc"],
    hdrs = ["resize_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "rgb_to_bgr_node",
    srcs = ["rgb_to_bgr_node.cc"],
    hdrs = ["rgb_to_bgr_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "softmax_node",
    srcs = ["softmax_node.cc"],
    hdrs = ["softmax_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "topk_node",
    srcs = ["topk_node.cc"],
    hdrs = ["topk_node.h"],
    deps = [
        "//src/pipeline:pipeline_node",
        "//src/pipeline:pipeline_node_factory",
        "//src/utils:types",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)