workspace(name = "atlas")

load("//:atlas_deps.bzl", "atlas_deps", "atlas_android_setup")

atlas_deps()

# Android NDK toolchain via rules_android_ndk (supports NDK r25b+).
# Requires ANDROID_NDK_HOME to be set. For non-Android builds this is harmless.
load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")

android_ndk_repository(
    name = "androidndk",
    api_level = 24,
)

bind(
    name = "android/crosstool",
    actual = "@androidndk//:toolchain",
)

atlas_android_setup()

# SNPE SDK.
#   snpe_major:   SNPE SDK major version (1 or 2, default 1).
#   snpe_sdk_path: path to SDK installation (falls back to $SNPE_SDK_PATH).
# When no path is available, @snpe_sdk is empty and the SNPE backend
# compiles as a stub.
load("//third_party/snpe:snpe_repo.bzl", "snpe_sdk_repo")
snpe_sdk_repo(
    name = "snpe_sdk",
    snpe_major = "1",
)