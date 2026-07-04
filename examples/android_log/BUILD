# Minimal Android logging example.

cc_binary(
    name = "android_log",
    srcs = ["main.cc"],
    # Force libc++ for this Android target so the main program matches the
    # SNPE runtime's C++ ABI and stays consistent with the device toolchain.
    copts = [
        "-DATLAS_ENABLE_LOGGING",
        "-DATLAS_ANDROID_LOG_ENABLE_SNPE_VERSION=1",
    ],
    deps = [
        "@snpe_sdk//:snpe",
        "//src/utils:logger",
    ],
)