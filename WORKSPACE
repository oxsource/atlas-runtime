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
