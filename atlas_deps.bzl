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
            url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-x64-1.17.3.tgz",
            sha256 = "f2f11f9da1e3e19b22a8b378b9af57a58433f40e3db6a803e75c0ec0eba97a20",
            strip_prefix = "onnxruntime-linux-x64-1.17.3",
            build_file = "@atlas//third_party:onnxruntime.BUILD",
        )

    if not native.existing_rule("onnxruntime_linux_aarch64"):
        http_archive(
            name = "onnxruntime_linux_aarch64",
            url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-aarch64-1.17.3.tgz",
            sha256 = "9f801577bd99676d1d821022e52b1f4554f56339ae3606c7b5ff3155f443c921",
            strip_prefix = "onnxruntime-linux-aarch64-1.17.3",
            build_file = "@atlas//third_party:onnxruntime.BUILD",
        )

    # Android AAR from Maven Central (contains arm64-v8a and x86_64 ABI .so files).
    # Two separate http_archive targets share the same URL but use ABI-specific
    # BUILD files, working around the limitation that external BUILD files cannot
    # reference //platforms:* from the main workspace.
    if not native.existing_rule("onnxruntime_android_arm64"):
        http_archive(
            name = "onnxruntime_android_arm64",
            url = "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.17.3/onnxruntime-android-1.17.3.aar",
            sha256 = "790d962102a47b9ed3523912cd9a39a67590cd353ae55d88c5358be7b6945d79",
            build_file = "@atlas//third_party:onnxruntime_android_arm64.BUILD",
        )

    if not native.existing_rule("onnxruntime_android_x86_64"):
        http_archive(
            name = "onnxruntime_android_x86_64",
            url = "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.17.3/onnxruntime-android-1.17.3.aar",
            sha256 = "790d962102a47b9ed3523912cd9a39a67590cd353ae55d88c5358be7b6945d79",
            build_file = "@atlas//third_party:onnxruntime_android_x86_64.BUILD",
        )

    # rules_android_ndk: external NDK rules that support NDK r25b+ with Bazel 6.5+.
    # Uses a pinned commit known to work with Bazel 6.5 + NDK r25/r27.
    if not native.existing_rule("rules_android_ndk"):
        http_archive(
            name = "rules_android_ndk",
            sha256 = "d230a980e0d3a42b85d5fce2cb17ec3ac52b88d2cff5aaf86bae0f05b48adc55",
            strip_prefix = "rules_android_ndk-d5c9d46a471e8fcd80e7ec5521b78bb2df48f4e0",
            url = "https://github.com/bazelbuild/rules_android_ndk/archive/d5c9d46a471e8fcd80e7ec5521b78bb2df48f4e0.zip",
        )

    if not native.existing_rule("googletest"):
        http_archive(
            name = "googletest",
            url = "https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz",
            sha256 = "81964fe578e9bd7c94dfdb09c8e4d6e6759e19967e397dbea48d1c10e45d0df2",
            strip_prefix = "googletest-release-1.12.1",
        )

def atlas_android_setup():
    """Configures Android NDK toolchain via rules_android_ndk (supports NDK r25+).

    Call this in addition to atlas_deps() when building Atlas for Android targets.
    Requires ANDROID_NDK_HOME environment variable to point to the NDK root,
    or set an explicit 'path' attribute.

    This function uses rules_android_ndk (not the built-in android_ndk_repository)
    which supports NDK r25b or later with Bazel 6.5+.

    Prerequisites:
      - Android NDK >= r25b; ANDROID_NDK_HOME must point to its root directory
      - Minimum API level 24 (first version with full 64-bit ABI support)

    Note: After calling atlas_android_setup(), you must also call in WORKSPACE:
      load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")
      android_ndk_repository(name = "androidndk", api_level = 24)
      bind(name = "android/crosstool", actual = "@androidndk//:toolchain")

    The bind() creates //external:android/crosstool which .bazelrc points to via
    --crosstool_top=//external:android/crosstool for Android builds.

    Example (external project WORKSPACE):
      load("@atlas//:atlas_deps.bzl", "atlas_deps")
      atlas_deps()  # downloads rules_android_ndk
      load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")
      android_ndk_repository(name = "androidndk", api_level = 24)
      bind(name = "android/crosstool", actual = "@androidndk//:toolchain")
    """
