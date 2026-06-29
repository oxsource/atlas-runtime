"""Platform build helpers for Atlas.

Provides config_setting_and_platform(), inspired by MediaPipe's platforms.bzl.
Each call creates:
  - A config_setting() usable inside select({"//platforms:<name>": ...})
  - A platform()       usable with --platforms=//platforms:<name>_platform

Both share the same constraint_values, so toolchain resolution and select()
conditions are always consistent.
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
