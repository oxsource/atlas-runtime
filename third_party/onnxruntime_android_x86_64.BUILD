# BUILD file for ONNX Runtime Android x86_64 prebuilt.
# Source: Maven Central com.microsoft.onnxruntime:onnxruntime-android:1.17.3
#
# AAR layout:
#   headers/   — C/C++ headers (flat, no subdirectory)
#   jni/x86_64/libonnxruntime.so

cc_library(
    name = "onnxruntime",
    hdrs = glob(["headers/*.h"]),
    includes = ["headers"],
    srcs = ["jni/x86_64/libonnxruntime.so"],
    visibility = ["//visibility:public"],
)
