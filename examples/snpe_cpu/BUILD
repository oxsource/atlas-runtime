# SNPE CPU runtime demonstration example.
# Uses the public //src/public:atlas target — no internal headers.
# Only functional on linux_aarch64 / android_arm64 (ATLAS_SNPE_ENABLED defined).
# On other platforms the SNPE stub returns kBackendNotFound.

cc_binary(
    name = "snpe_cpu",
    srcs = ["main.cc"],
    data = ["manifest.json"],
    deps = [
        "//src/public:atlas",
    ],
)