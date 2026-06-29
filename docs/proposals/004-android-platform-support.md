# Proposal-004: Android 平台构建支持

> **提议日期**：2026-06-29
> **提议人**：pizzk <726676435@qq.com>
> **状态**：草案
> **类型**：模块级
> **关联**：`docs/proposals/003-platform-build-refinement.md`（平台体系基础）、`atlas_deps.bzl`、`platforms/BUILD`

---

## 一、背景与动机

Proposal-003 建立了基于 `config_setting_and_platform` 的平台体系，并预留了 Android 扩展位。本提案在此基础上补全 Android 端到端构建支持，目标是让 Atlas 的 CPU 推理能力可交叉编译到 Android arm64-v8a 和 x86_64 设备，为移动端 / 边缘端部署提供基础。

### 1.1 当前缺失

| 层面 | 现状 |
|------|------|
| 平台定义 | `platforms/BUILD` 尚无 `android_arm64` / `android_x86_64` |
| ONNX Runtime | 无 Android 预编译库依赖 |
| NDK 工具链 | `WORKSPACE` 未配置 `android_ndk_repository` |
| `select()` | CPU/公共 BUILD 无 Android 分支 |
| `.bazelrc` | 无 Android 交叉编译快捷配置 |

---

## 二、方案概要

### 2.1 核心思路

1. **平台补全**：在 `platforms/BUILD` 新增 `android_arm64` 和 `android_x86_64`。
2. **ONNX Runtime Android**：v1.17.3 发布了 `onnxruntime-android-1.17.3.aar`（ZIP 格式），内含多 ABI 的 `.so` 和 C/C++ 头文件。为 Bazel 引入两个独立 `http_archive` 目标（`onnxruntime_android_arm64` / `onnxruntime_android_x86_64`），各使用 ABI 专属 BUILD 文件，彻底规避外部仓库上下文中无法引用 `//platforms:*` 的限制。
3. **NDK 工具链**：在 `WORKSPACE`（通过 `atlas_deps.bzl`）声明 `android_ndk_repository`；NDK 路径由 `ANDROID_NDK_HOME` 环境变量提供，无需硬编码。
4. **SELECT 扩展**：`src/backend/cpu/BUILD` 和 `src/public/BUILD` 新增 Android 分支。
5. **`.bazelrc`**：追加 `build:android_arm64` 和 `build:android_x86_64` 快捷配置。

---

## 三、详细设计

### 3.1 平台定义扩展（`platforms/BUILD`）

```starlark
# Android arm64-v8a
config_setting_and_platform(
    name = "android_arm64",
    constraint_values = [
        "@platforms//os:android",
        "@platforms//cpu:arm64",
    ],
)

# Android x86_64（模拟器 / x86_64 设备）
config_setting_and_platform(
    name = "android_x86_64",
    constraint_values = [
        "@platforms//os:android",
        "@platforms//cpu:x86_64",
    ],
)
```

### 3.2 AAR 内部结构说明

`onnxruntime-android-1.17.3.aar` 是标准 ZIP，解压后布局：

```
onnxruntime-android-1.17.3.aar
├── headers/                        ← C/C++ 头文件（所有 ABI 共享）
│   └── onnxruntime_cxx_api.h
│   └── onnxruntime_c_api.h
│   └── ...
├── jni/
│   ├── arm64-v8a/
│   │   └── libonnxruntime.so
│   └── x86_64/
│       └── libonnxruntime.so
└── AndroidManifest.xml
```

> 实际路径在实现前须通过 `unzip -l onnxruntime-android-1.17.3.aar` 验证。

### 3.3 `atlas_deps.bzl` 扩展

两个 http_archive 指向同一 AAR URL，分别绑定不同 BUILD 文件：

```starlark
if not native.existing_rule("onnxruntime_android_arm64"):
    http_archive(
        name = "onnxruntime_android_arm64",
        url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-android-1.17.3.aar",
        sha256 = "<sha256-to-be-filled>",  # 使用 Bazel 报告的真实 hash 回填
        build_file = "@atlas//third_party:onnxruntime_android_arm64.BUILD",
    )

if not native.existing_rule("onnxruntime_android_x86_64"):
    http_archive(
        name = "onnxruntime_android_x86_64",
        url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-android-1.17.3.aar",
        sha256 = "<sha256-to-be-filled>",
        build_file = "@atlas//third_party:onnxruntime_android_x86_64.BUILD",
    )
```

> **sha256 获取方法**：先填占位符，执行 `bazel build` 后 Bazel 会输出真实 hash，回填即可。

### 3.4 新增 BUILD 文件

**`third_party/onnxruntime_android_arm64.BUILD`**

```starlark
# BUILD file for ONNX Runtime Android arm64-v8a prebuilt.
# AAR layout: headers/ contains C/C++ headers; jni/arm64-v8a/ contains .so.

cc_library(
    name = "onnxruntime",
    hdrs = glob(["headers/**/*.h"]),
    includes = ["headers"],
    srcs = ["jni/arm64-v8a/libonnxruntime.so"],
    visibility = ["//visibility:public"],
)
```

**`third_party/onnxruntime_android_x86_64.BUILD`**

```starlark
# BUILD file for ONNX Runtime Android x86_64 prebuilt.

cc_library(
    name = "onnxruntime",
    hdrs = glob(["headers/**/*.h"]),
    includes = ["headers"],
    srcs = ["jni/x86_64/libonnxruntime.so"],
    visibility = ["//visibility:public"],
)
```

### 3.5 NDK 工具链（`atlas_deps.bzl`）

```starlark
def atlas_android_setup():
    """Configures Android NDK toolchain.

    Must be called separately from atlas_deps() because android_ndk_repository
    is a WORKSPACE rule, not a regular repository rule.
    Requires ANDROID_NDK_HOME environment variable to be set,
    or an explicit 'path' argument to be provided.

    Recommended NDK: r25c or later (full C++17 support).
    Minimum API level: 24 (first version with full 64-bit ABI support).
    """
    native.android_ndk_repository(
        name = "androidndk",
        api_level = 24,
    )
```

外部项目 WORKSPACE 中调用：

```starlark
load("@atlas//:atlas_deps.bzl", "atlas_deps", "atlas_android_setup")
atlas_deps()
atlas_android_setup()  # 仅 Android 目标需要调用
```

> `android_ndk_repository` 是 WORKSPACE 内置规则，无法放入 `atlas_deps()` 的 `http_archive` 流程中，因此单独封装为 `atlas_android_setup()`。

### 3.6 `select()` 扩展

`src/backend/cpu/BUILD` 和 `src/public/BUILD` 新增 Android 分支：

```starlark
select({
    "//platforms:macos_arm64":    ["@onnxruntime_macos_arm64//:onnxruntime"],
    "//platforms:macos_x86_64":   ["@onnxruntime_macos_arm64//:onnxruntime"],
    "//platforms:linux_aarch64":  ["@onnxruntime_linux_aarch64//:onnxruntime"],
    "//platforms:android_arm64":  ["@onnxruntime_android_arm64//:onnxruntime"],
    "//platforms:android_x86_64": ["@onnxruntime_android_x86_64//:onnxruntime"],
    "//conditions:default":       ["@onnxruntime_linux_x86_64//:onnxruntime"],
})
```

### 3.7 `.bazelrc` 追加

```ini
# Android cross-compilation
# Prerequisites: ANDROID_NDK_HOME must be set in the environment.
build:android_arm64   --platforms=//platforms:android_arm64_platform
build:android_arm64   --android_ndk_api_level=24

build:android_x86_64  --platforms=//platforms:android_x86_64_platform
build:android_x86_64  --android_ndk_api_level=24
```

使用示例：

```bash
export ANDROID_NDK_HOME=$HOME/Library/Android/sdk/ndk/25.2.9519653
bazel build //src/public:atlas --config=android_arm64
```

---

## 四、受影响文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `platforms/BUILD` | 修改 | 新增 `android_arm64`、`android_x86_64` |
| `atlas_deps.bzl` | 修改 | 新增 `onnxruntime_android_arm64/x86_64`、`atlas_android_setup()` |
| `third_party/onnxruntime_android_arm64.BUILD` | 新增 | arm64-v8a ABI 专属 BUILD |
| `third_party/onnxruntime_android_x86_64.BUILD` | 新增 | x86_64 ABI 专属 BUILD |
| `src/backend/cpu/BUILD` | 修改 | `select()` 新增 Android 分支（2 处） |
| `src/public/BUILD` | 修改 | `select()` 新增 Android 分支（1 处） |
| `WORKSPACE` | 修改 | 调用 `atlas_android_setup()` |
| `.bazelrc` | 修改 | 追加 Android 快捷配置 |

---

## 五、不包含的内容

- Android Java / Kotlin 绑定（不使用 AAR 的 Java 层）
- Android GPU / NNAPI 执行提供者（需单独 ONNX Runtime 构建选项）
- armeabi-v7a 32 位 ABI（已被多数现代设备和 ONNX Runtime 官方逐步弃用）
- Android 应用打包（APK/AAB 构建，需 `rules_android`）
- CI 自动化 Android 测试（需连接设备或模拟器，超出本提案范围）

---

## 六、前置条件

| 条件 | 说明 |
|------|------|
| Android NDK ≥ r25c | 完整 C++17 支持；`ANDROID_NDK_HOME` 需指向 NDK 根目录 |
| Bazel 6.5 | 内置 `android_ndk_repository`，无需额外依赖 |
| Proposal-003 已实现 | 本提案基于其平台框架扩展 |

---

## 七、实现检查清单

- [ ] 验证 AAR 实际内部路径（`unzip -l onnxruntime-android-1.17.3.aar`）
- [ ] 获取 AAR 真实 sha256（`sha256sum onnxruntime-android-1.17.3.aar`）
- [ ] 创建 `third_party/onnxruntime_android_arm64.BUILD`
- [ ] 创建 `third_party/onnxruntime_android_x86_64.BUILD`
- [ ] 更新 `platforms/BUILD` 新增 Android 平台
- [ ] 更新 `atlas_deps.bzl`（新增 AAR 两个 archive + `atlas_android_setup()`）
- [ ] 更新 `WORKSPACE` 调用 `atlas_android_setup()`
- [ ] 迁移 `src/backend/cpu/BUILD` 和 `src/public/BUILD` 的 `select()`
- [ ] 追加 `.bazelrc` Android 快捷配置
- [ ] `bazel build //src/public:atlas --config=android_arm64` 编译通过
- [ ] 实现完成后更新本文档状态为「已采纳」并归档
