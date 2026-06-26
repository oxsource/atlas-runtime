"""Convenience macros for external Bazel projects consuming Atlas."""

def atlas_linkopts():
    """Returns linkopts needed when linking against libatlas.

    Sets RPATH so the runtime linker can find libatlas.so and its
    bundled ONNX Runtime dependency relative to the executable.
    """
    return select({
        "@bazel_tools//src/conditions:darwin": [
            "-Wl,-rpath,@loader_path/../lib",
        ],
        "//conditions:default": [
            "-Wl,-rpath,$ORIGIN/../lib",
        ],
    })
