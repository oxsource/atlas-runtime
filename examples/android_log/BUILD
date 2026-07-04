# Minimal Android logging example.

cc_binary(
    name = "android_log",
    srcs = ["main.cc"],
    copts = ["-DATLAS_ENABLE_LOGGING"],
    deps = [
        "//src/utils:logger",
    ],
)