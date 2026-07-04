"""Reusable ONNX Runtime Bazel dependency definitions."""


def onnxruntime_deps():
    """Returns platform-selected ONNX Runtime dependency list."""
    return select({
        "//platforms:macos_arm64": ["@onnxruntime_macos_arm64//:onnxruntime"],
        "//platforms:macos_x86_64": ["@onnxruntime_macos_arm64//:onnxruntime"],
        "//platforms:linux_aarch64": ["@onnxruntime_linux_aarch64//:onnxruntime"],
        "//platforms:android_arm64": ["@onnxruntime_android_arm64//:onnxruntime"],
        "//platforms:android_x86_64": ["@onnxruntime_android_x86_64//:onnxruntime"],
        "//conditions:default": ["@onnxruntime_linux_x86_64//:onnxruntime"],
    })
