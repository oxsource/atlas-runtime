# This example uses ONLY the public //src/public:atlas target,
# demonstrating how an external project would consume Atlas.
# No internal headers (src/...) are referenced.

cc_binary(
    name = "two_model_pipeline",
    srcs = ["main.cc"],
    data = ["manifest.json"],
    deps = [
        "//src/public:atlas",
    ],
)
