"""Platform build helpers for Atlas.

Provides:
  - config_setting_and_platform(): create config_setting + platform pairs.
  - atlas_select(): convenience select() for consumers, maps logical
    platform names to @oxsource_atlas//platforms:* labels.
"""

def config_setting_and_platform(name, constraint_values):
    """Creates a config_setting and a platform with identical constraints.

    Args:
        name: Base name. config_setting is named <name>;
              platform is named <name>_platform.
        constraint_values: List of constraint_value labels.
    """
    native.config_setting(
        name = name,
        constraint_values = constraint_values,
        visibility = ["//visibility:public"],
    )
    native.platform(
        name = name + "_platform",
        constraint_values = constraint_values,
        visibility = ["//visibility:public"],
    )

def atlas_select(
        macos_arm64 = None,
        macos_x86_64 = None,
        android_arm64 = None,
        android_x86_64 = None,
        linux_aarch64 = None,
        linux_x86_64 = None,
        macos = None,
        android = None,
        linux = None,
        default = None):
    """Platform-aware select() for Atlas consumers.

    Maps logical platform names to the ``config_setting`` labels defined
    in ``@oxsource_atlas//platforms:BUILD``.

    All values fall back to ``default`` if not provided (e.g.
    ``macos_arm64`` → ``macos`` → ``default``).

    Example (consumer BUILD file):

    .. code:: python

        load("@oxsource_atlas//platforms:platforms.bzl", "atlas_select")

        linkopts = atlas_select(
            macos = ["-Wl,-install_name,@rpath/libfoo.dylib"],
            default = ["-Wl,-soname,libfoo.so"],
        )

    Args:
      macos_arm64:  Value for macOS Apple Silicon.
      macos_x86_64: Value for macOS Intel.
      android_arm64:  Value for Android arm64-v8a.
      android_x86_64: Value for Android x86_64 (emulator).
      linux_aarch64:  Value for Linux AArch64.
      linux_x86_64:   Value for Linux x86-64.
      macos:  Fallback for any macOS.
      android: Fallback for any Android.
      linux:  Fallback for any Linux.
      default:  Fallback value (``//conditions:default``).

    Returns:
      A ``select()`` dict suitable for ``linkopts``, ``deps``, etc.
    """
    return select({
        "@oxsource_atlas//platforms:macos_arm64":   macos_arm64 or macos or default,
        "@oxsource_atlas//platforms:macos_x86_64":  macos_x86_64 or macos or default,
        "@oxsource_atlas//platforms:android_arm64": android_arm64 or android or default,
        "@oxsource_atlas//platforms:android_x86_64": android_x86_64 or android or default,
        "@oxsource_atlas//platforms:linux_aarch64": linux_aarch64 or linux or default,
        "@oxsource_atlas//platforms:linux_x86_64":  linux_x86_64 or linux or default,
        "//conditions:default": default,
    })
