# Minimal Android logging example.

cc_binary(
    name = "android_log",
    srcs = ["main.cc"],
    # Force libc++ for this Android target so the main program matches the
    # SNPE runtime's C++ ABI and stays consistent with the device toolchain.
    copts = [
        "-DATLAS_ENABLE_LOGGING",
    ],
    deps = [
        "//src/backend/snpe:snpe_backend",
        "//src/utils:logger",
    ],
)
