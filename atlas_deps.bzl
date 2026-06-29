"""Dependency bootstrap macro for external Bazel projects.

Usage in external project's WORKSPACE:

    load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

    http_archive(
        name = "atlas",
        url = "https://github.com/<org>/atlas/archive/refs/tags/v1.0.0.tar.gz",
        sha256 = "<sha256-of-release-archive>",
        strip_prefix = "atlas-1.0.0",
    )

    load("@atlas//:atlas_deps.bzl", "atlas_deps")
    atlas_deps()

This ensures Atlas's transitive dependencies (ONNX Runtime, nlohmann/json,
GoogleTest) are fetched with the correct versions.
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def atlas_deps():
    """Fetches all third-party dependencies required by Atlas."""
    if not native.existing_rule("nlohmann_json"):
        http_archive(
            name = "nlohmann_json",
            url = "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz",
            sha256 = "0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406",
            strip_prefix = "json-3.11.3",
            build_file = "@atlas//third_party:nlohmann_json.BUILD",
        )

    if not native.existing_rule("onnxruntime_macos_arm64"):
        http_archive(
            name = "onnxruntime_macos_arm64",
            url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-osx-arm64-1.17.3.tgz",
            sha256 = "236c49c9065213b0ec9dec874e3619da3d01cbc8b984bb24291247293454d0f4",
            strip_prefix = "onnxruntime-osx-arm64-1.17.3",
            build_file = "@atlas//third_party:onnxruntime.BUILD",
        )

    if not native.existing_rule("onnxruntime_linux_x86_64"):
        http_archive(
            name = "onnxruntime_linux_x86_64",
            url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-x86_64-1.17.3.tgz",
            sha256 = "0019dfc4b32d63c1392aa264aed2253c1e0c2fb09216f8e2cc269bbfb8bb49b5",
            strip_prefix = "onnxruntime-linux-x86_64-1.17.3",
            build_file = "@atlas//third_party:onnxruntime.BUILD",
        )

    if not native.existing_rule("googletest"):
        http_archive(
            name = "googletest",
            url = "https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz",
            sha256 = "81964fe578e9bd7c94dfdb09c8e4d6e6759e19967e397dbea48d1c10e45d0df2",
            strip_prefix = "googletest-release-1.12.1",
        )
