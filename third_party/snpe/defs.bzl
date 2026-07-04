"""Reusable SNPE Bazel definitions.

This file centralizes platform selects and version-specific source selection
for SNPE targets so internal and external BUILD files can share one contract.
"""

load("@snpe_sdk//:version.bzl", "SNPE_MAJOR")


def snpe_platform_defines():
    """Returns ATLAS_SNPE_ENABLED define on supported target platforms."""
    return select({
        "//platforms:linux_aarch64": ["ATLAS_SNPE_ENABLED=1"],
        "//platforms:linux_x86_64": ["ATLAS_SNPE_ENABLED=1"],
        "//platforms:android_arm64": ["ATLAS_SNPE_ENABLED=1"],
        "//conditions:default": [],
    })


def snpe_sdk_deps():
    """Returns SNPE SDK dependency list for supported target platforms."""
    return select({
        "//platforms:linux_aarch64": ["@snpe_sdk//:snpe"],
        "//platforms:linux_x86_64": ["@snpe_sdk//:snpe"],
        "//platforms:android_arm64": ["@snpe_sdk//:snpe"],
        "//conditions:default": [],
    })


def snpe_backend_srcs():
    """Returns backend implementation source list selected by platform/version."""
    backend_v = ["snpe_backend_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR else ["snpe_backend_stub.cc"]
    return select({
        "//platforms:linux_aarch64": backend_v,
        "//platforms:linux_x86_64": backend_v,
        "//platforms:android_arm64": backend_v,
        "//conditions:default": ["snpe_backend_stub.cc"],
    })


def snpe_context_srcs():
    """Returns backend context source list selected by platform/version."""
    context_v = ["snpe_backend_context_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR else ["snpe_backend_context_stub.cc"]
    return select({
        "//platforms:linux_aarch64": context_v,
        "//platforms:linux_x86_64": context_v,
        "//platforms:android_arm64": context_v,
        "//conditions:default": ["snpe_backend_context_stub.cc"],
    })
