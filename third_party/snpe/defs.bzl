"""Reusable SNPE Bazel definitions.

This file centralizes platform selects and version-specific source selection
for SNPE targets so internal and external BUILD files can share one contract.
"""

load("@snpe_sdk//:version.bzl", "SNPE_MAJOR")
load("@oxsource_atlas//platforms:platforms.bzl", "atlas_select")


def snpe_platform_defines():
    """Returns ATLAS_SNPE_ENABLED define on supported target platforms."""
    return atlas_select(
        linux_aarch64 = ["ATLAS_SNPE_ENABLED=1"],
        linux_x86_64  = ["ATLAS_SNPE_ENABLED=1"],
        android_arm64 = ["ATLAS_SNPE_ENABLED=1"],
        default       = [],
    )


def snpe_sdk_deps():
    """Returns SNPE SDK dependency list for supported target platforms."""
    if SNPE_MAJOR == None:
        return []
    return atlas_select(
        linux_aarch64 = ["@snpe_sdk//:snpe"],
        linux_x86_64  = ["@snpe_sdk//:snpe"],
        android_arm64 = ["@snpe_sdk//:snpe"],
        default       = [],
    )


def snpe_backend_srcs():
    """Returns backend implementation source list selected by platform/version."""
    backend_v = ["snpe_backend_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR else ["snpe_backend_stub.cc"]
    return atlas_select(
        linux_aarch64 = backend_v,
        linux_x86_64  = backend_v,
        android_arm64 = backend_v,
        default       = ["snpe_backend_stub.cc"],
    )


def snpe_context_srcs():
    """Returns backend context source list selected by platform/version."""
    context_v = ["snpe_backend_context_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR else ["snpe_backend_context_stub.cc"]
    return atlas_select(
        linux_aarch64 = context_v,
        linux_x86_64  = context_v,
        android_arm64 = context_v,
        default       = ["snpe_backend_context_stub.cc"],
    )
