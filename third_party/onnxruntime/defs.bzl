"""Reusable ONNX Runtime Bazel dependency definitions."""

load("@oxsource_atlas//platforms:platforms.bzl", "atlas_select")


def onnxruntime_deps():
    """Returns platform-selected ONNX Runtime dependency list."""
    return atlas_select(
        macos_arm64    = ["@onnxruntime_macos_arm64//:onnxruntime"],
        macos_x86_64   = ["@onnxruntime_macos_arm64//:onnxruntime"],
        linux_aarch64  = ["@onnxruntime_linux_aarch64//:onnxruntime"],
        android_arm64  = ["@onnxruntime_android_arm64//:onnxruntime"],
        android_x86_64 = ["@onnxruntime_android_x86_64//:onnxruntime"],
        default        = ["@onnxruntime_linux_x86_64//:onnxruntime"],
    )
