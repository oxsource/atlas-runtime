"""Custom repository rule that creates @snpe_sdk from the SNPE SDK installation.

Version is declared via the ``snpe_major`` attribute (1 or 2, default 2).
The corresponding ``snpe_v{N}.BUILD`` template is rendered into
``@snpe_sdk//:BUILD.bazel``.  Path is taken from ``snpe_sdk_path`` attribute,
falling back to the ``$SNPE_SDK_PATH`` environment variable when the attribute
is empty.

When no SDK path is available, an empty repository is created and
``SNPE_MAJOR = None`` is exported — the SNPE backend compiles as stub.
"""

def _snpe_sdk_repo_impl(repository_ctx):
    major = repository_ctx.attr.snpe_major
    sdk_path = repository_ctx.attr.snpe_sdk_path
    if not sdk_path:
        sdk_path = repository_ctx.os.environ.get("SNPE_SDK_PATH", "")

    if not sdk_path:
        # No SDK available — empty repo, stub compile.
        repository_ctx.file("BUILD.bazel", "")
        repository_ctx.file("version.bzl", "SNPE_MAJOR = None\n")
        return

    repository_ctx.symlink(sdk_path, "snpe_sdk_root")

    # Render the version-specific BUILD template.
    repository_ctx.template(
        "BUILD.bazel",
        Label("//third_party/snpe:snpe_v%s.BUILD" % major),
    )

    # Export SNPE_MAJOR for src/backend/snpe/BUILD.
    repository_ctx.file("version.bzl",
                        'SNPE_MAJOR = "{}"\n'.format(major))

snpe_sdk_repo = repository_rule(
    implementation = _snpe_sdk_repo_impl,
    attrs = {
        "snpe_major": attr.string(
            default = "2",
            values = ["1", "2"],
            doc = "SNPE SDK major version (1 or 2).",
        ),
        "snpe_sdk_path": attr.string(
            doc = "Absolute path to the SNPE SDK installation directory. "
                  + "Falls back to the $SNPE_SDK_PATH environment variable.",
        ),
    },
    environ = ["SNPE_SDK_PATH"],
    local = True,
    doc = "Creates @snpe_sdk from a local SNPE SDK installation.",
)