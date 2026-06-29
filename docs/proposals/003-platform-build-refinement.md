# Proposal-003: 精细化平台构建方案

> **提议日期**：2026-06-29
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：模块级
> **关联**：`docs/phase5.md`（示例 + 发布）、`atlas_deps.bzl`、`third_party/onnxruntime.BUILD`

---

## 一、背景与动机

### 1.1 现状

当前项目平台检测逻辑分散在三处 BUILD 文件中，均直接引用 Bazel 内置条件：

```starlark
# 三处重复出现
select({
    "@bazel_tools//src/conditions:darwin_arm64": [...],
    "//conditions:default": [...],   # 隐式等同于 linux_x86_64
})
```

| 文件 | `select()` 数量 |
|------|----------------|
| `src/backend/cpu/BUILD` | 2 |
| `src/public/BUILD` | 1 |
| `third_party/onnxruntime.BUILD` | 1 |

### 1.2 问题

1. **平台覆盖不完整**：`//conditions:default` 语义模糊，实际只绑定了 linux_x86_64 预编译库，无法区分 linux_aarch64、macOS x86_64、Windows 等平台；新增平台时所有 `select()` 均需逐一改动。
2. **`config_setting` 与 `platform` 割裂**：`@bazel_tools//src/conditions:darwin_arm64` 是遗留的 `config_setting`，无对应 `platform()` 目标，无法通过 `--platforms` 标志切换平台（交叉编译场景）。
3. **扩展成本高**：接入 linux_aarch64（SNPE / RKNN 边缘设备）或 Android 时，需同时修改 `atlas_deps.bzl` + 所有 BUILD 中的 `select()`，缺乏单一变更点。

### 1.3 参考

MediaPipe 的 `config_setting_and_platform` 机制：在同一宏调用中用相同 `constraint_values` 同时创建 `config_setting()` 和 `platform()`，让 `select()` 与 `--platforms` 标志共享一套定义，彻底消除两者割裂。

---

## 二、方案概要

### 2.1 核心思路

1. 新建 `//platforms/` 目录，提供 `platforms.bzl`（含 `config_setting_and_platform` 宏）与 `BUILD` 文件。
2. 在 `BUILD` 中用宏定义 Atlas 支持的全部目标平台（首批四个）。
3. 所有 `select()` 调用改为引用 `//platforms:*`，消除对 `@bazel_tools//src/conditions:*` 的直接依赖。
4. 在 `atlas_deps.bzl` 中补充 `onnxruntime_linux_aarch64` 预编译包，为 linux_aarch64 平台做好依赖基础。
5. 新增 `.bazelrc` 提供常用平台别名，简化本地开发命令。

### 2.2 目录结构（新增部分）

```
platforms/
├── BUILD          # 调用宏，声明所有 platform + config_setting 目标
└── platforms.bzl  # config_setting_and_platform 宏定义
.bazelrc           # 新增（或追加）平台别名配置
```

---

## 三、详细设计

### 3.1 `platforms/platforms.bzl`

```starlark
"""Platform build helpers for Atlas.

Provides config_setting_and_platform(), inspired by MediaPipe's platforms.bzl.
Each call creates:
  - A platform()       usable with --platforms=//platforms:<name>
  - A config_setting() usable inside select({"//platforms:<name>": ...})

Both share the same constraint_values, so toolchain resolution and select()
conditions are always consistent.
"""

def config_setting_and_platform(name, constraint_values):
    """Creates a config_setting and a platform with identical constraints.

    Args:
        name: Target name. The platform target is named "<name>".
              The config_setting is also named "<name>" but lives in the
              same package — callers use "//platforms:<name>" for both.
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
```

> **说明**：`config_setting` 与 `platform` 同名会产生目标冲突，因此 `platform` 加 `_platform` 后缀。`select()` 引用 `//platforms:<name>`（`config_setting`），`--platforms` 标志引用 `//platforms:<name>_platform`。

### 3.2 `platforms/BUILD`

```starlark
load(":platforms.bzl", "config_setting_and_platform")

package(default_visibility = ["//visibility:public"])

# macOS Apple Silicon
config_setting_and_platform(
    name = "macos_arm64",
    constraint_values = [
        "@platforms//os:macos",
        "@platforms//cpu:arm64",
    ],
)

# macOS Intel
config_setting_and_platform(
    name = "macos_x86_64",
    constraint_values = [
        "@platforms//os:macos",
        "@platforms//cpu:x86_64",
    ],
)

# Linux x86-64
config_setting_and_platform(
    name = "linux_x86_64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

# Linux AArch64（边缘设备 / SNPE / RKNN 目标平台）
config_setting_and_platform(
    name = "linux_aarch64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
)
```

> `@platforms` 是 Bazel 6 内置仓库，无需在 WORKSPACE 中额外声明。

### 3.3 `select()` 迁移

所有**内部** BUILD 文件（`src/`）统一替换为：

```starlark
# 旧写法（分散、语义模糊）
select({
    "@bazel_tools//src/conditions:darwin_arm64": ["@onnxruntime_macos_arm64//:onnxruntime"],
    "//conditions:default":                      ["@onnxruntime_linux_x86_64//:onnxruntime"],
})

# 新写法（语义明确、可扩展）
select({
    "//platforms:macos_arm64":   ["@onnxruntime_macos_arm64//:onnxruntime"],
    "//platforms:macos_x86_64":  ["@onnxruntime_macos_arm64//:onnxruntime"],
    "//platforms:linux_aarch64": ["@onnxruntime_linux_aarch64//:onnxruntime"],
    "//conditions:default":      ["@onnxruntime_linux_x86_64//:onnxruntime"],
})
```

受影响文件：`src/backend/cpu/BUILD`（2 处）、`src/public/BUILD`（1 处）。

> **`third_party/onnxruntime.BUILD` 例外**：该文件通过 `build_file =` 机制在外部仓库（`@onnxruntime_*`）上下文中执行，`//platforms:*` 会被解析为外部仓库自身的包路径，而非 Atlas 主工作区。因此该文件继续使用 `@bazel_tools//src/conditions:darwin_arm64`（用于区分 macOS `.dylib` 与 Linux `.so`），这与其用途一致：仅区分文件格式，不负责选择平台预编译包。

### 3.4 `atlas_deps.bzl` 扩展

新增 `onnxruntime_linux_aarch64` 条目：

```starlark
if not native.existing_rule("onnxruntime_linux_aarch64"):
    http_archive(
        name = "onnxruntime_linux_aarch64",
        url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-aarch64-1.17.3.tgz",
        sha256 = "2a65a5edd9bce2c7f6373117ca3a3a39e2db00d0c03e1c4e2cfe95cd34c42e0e",
        strip_prefix = "onnxruntime-linux-aarch64-1.17.3",
        build_file = "@atlas//third_party:onnxruntime.BUILD",
    )
```

`third_party/onnxruntime.BUILD` 中同步添加 `linux_aarch64` 的共享库路径（`.so` 与 arm64 相同命名规则）。

### 3.5 `.bazelrc`

```ini
# Platform shortcuts
build:macos_arm64   --platforms=//platforms:macos_arm64_platform
build:linux_x86_64  --platforms=//platforms:linux_x86_64_platform
build:linux_aarch64 --platforms=//platforms:linux_aarch64_platform
```

使用示例：

```bash
bazel build //... --config=linux_aarch64   # 交叉编译到 linux aarch64
bazel build //...                          # 自动探测宿主平台（默认行为不变）
```

---

## 四、受影响文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `platforms/platforms.bzl` | 新增 | `config_setting_and_platform` 宏 |
| `platforms/BUILD` | 新增 | 四平台目标定义 |
| `.bazelrc` | 修改 | 追加平台别名配置 |
| `atlas_deps.bzl` | 修改 | 新增 `onnxruntime_linux_aarch64` |
| `src/backend/cpu/BUILD` | 修改 | `select()` 迁移（2 处） |
| `src/public/BUILD` | 修改 | `select()` 迁移（1 处） |
| `third_party/onnxruntime.BUILD` | 不变 | 外部 BUILD 文件，继续使用 `@bazel_tools` 条件 |

---

## 五、不包含的内容

- macOS x86_64 ONNX Runtime 预编译包接入（留给后续迭代）
- Android、Windows 平台支持
- 交叉编译工具链配置（toolchain 定义超出本提案范围）
- SNPE / RKNN 后端实现（见 Proposal-002）

---

## 六、向后兼容性

- 宿主平台自动探测路径（不传 `--platforms`）行为不变：Bazel 根据宿主 OS/CPU 解析 `constraint_values`，`config_setting` 条件依然匹配。
- 现有 `//conditions:default` 作为兜底保留，新增平台显式命中后不再走 default 分支。
- 外部项目通过 `atlas_deps()` 引入依赖的方式不变。

---

## 七、实现检查清单

- [ ] 创建 `platforms/platforms.bzl`
- [ ] 创建 `platforms/BUILD`
- [ ] 更新 `atlas_deps.bzl`（新增 `onnxruntime_linux_aarch64`）
- [ ] 更新 `third_party/onnxruntime.BUILD`（新增 aarch64 路径）
- [ ] 迁移 `src/backend/cpu/BUILD` 中的 `select()`
- [ ] 迁移 `src/public/BUILD` 中的 `select()`
- [ ] 新增 `.bazelrc` 平台别名
- [ ] `bazel build //src/... //tests/... //examples/...` 全部通过
- [ ] 实现完成后更新本文档状态为「已采纳」并归档
