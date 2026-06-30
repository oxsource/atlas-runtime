# BUILD file for ONNX Runtime prebuilt binaries.
# Both macOS arm64 and Linux x86_64 archives share the same internal layout:
#   include/  — C/C++ headers
#   lib/      — shared library

cc_library(
    name = "onnxruntime",
    hdrs = glob(["include/*.h"]),
    includes = ["include"],
    srcs = select({
        "@bazel_tools//src/conditions:darwin_arm64": [
            "lib/libonnxruntime.dylib",
            "lib/libonnxruntime.1.17.3.dylib",
        ],
        "//conditions:default": [
            "lib/libonnxruntime.so",
            "lib/libonnxruntime.so.1.17.3",
        ],
    }),
    visibility = ["//visibility:public"],
)
