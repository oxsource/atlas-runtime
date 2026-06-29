# BUILD file for ONNX Runtime Android arm64-v8a prebuilt.
# Source: Maven Central com.microsoft.onnxruntime:onnxruntime-android:1.17.3
#
# AAR layout:
#   headers/   — C/C++ headers (flat, no subdirectory)
#   jni/arm64-v8a/libonnxruntime.so

cc_library(
    name = "onnxruntime",
    hdrs = glob(["headers/*.h"]),
    includes = ["headers"],
    srcs = ["jni/arm64-v8a/libonnxruntime.so"],
    visibility = ["//visibility:public"],
)
