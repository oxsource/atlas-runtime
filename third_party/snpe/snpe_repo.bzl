"""Custom repository rule that creates @snpe_sdk from the $SNPE_SDK_PATH
environment variable via a symlink (zero-copy).

When $SNPE_SDK_PATH is not set, an empty repository is created — the cc_library
targets that depend on @snpe_sdk//:snpe will fall through to the
//conditions:default branch of their select() and compile as stub.
"""

def _snpe_sdk_repo_impl(repository_ctx):
    snpe_path = repository_ctx.os.environ.get("SNPE_SDK_PATH", "")
    if snpe_path:
        repository_ctx.symlink(snpe_path, "snpe_sdk_root")
    # Always generate BUILD from the template so that @snpe_sdk//:snpe exists.
    # When SNPE_SDK_PATH is empty, no .so files match and the cc_library is empty.
    repository_ctx.template(
        "BUILD.bazel",
        Label("//third_party/snpe:snpe.BUILD"),
    )

snpe_sdk_repo = repository_rule(
    implementation = _snpe_sdk_repo_impl,
    environ = ["SNPE_SDK_PATH"],
    local = True,
    doc = "Creates @snpe_sdk from $SNPE_SDK_PATH via symlink (zero-copy).",
)
