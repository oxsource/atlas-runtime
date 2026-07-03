# Proposal-002: SNPE 后端接入

> **提议日期**：2026-06-26
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：阶段级
> **关联**：`docs/architecture.md` 第 3.3 节后端抽象层、阶段四「TensorRT / RKNN / SNPE 后端按需接入」

## 一、背景

`architecture.md` 阶段四规划了 TensorRT / RKNN / SNPE 后端的按需接入。当前项目仅实现了 CpuBackend（ONNX Runtime），后端抽象层（`IBackend` / `IBackendContext` / `BackendFactory`）已就绪，新增后端只需：

1. 实现 `IBackend` 接口（Load / Infer / GetInputInfo / GetOutputInfo / Unload / IsLoaded）
2. 实现 `IBackendContext` 接口（BackendType + 共享运行时资源）
3. 通过 `ATLAS_REGISTER_BACKEND` / `ATLAS_REGISTER_BACKEND_CONTEXT` 宏注册
4. 在 BUILD 中设置 `alwayslink = 1`

SNPE（Snapdragon Neural Processing Engine）是高通的 AI 推理引擎，支持在 Qualcomm DSP / HTP 上执行量化模型，适用于移动端 / 边缘设备的低延迟推理场景。

## 二、方案概要

### 核心思路

1. **新增 `src/backend/snpe/` 目录**，实现 `SnpeBackend` 与 `SnpeBackendContext`
2. **SNPE SDK 依赖管理**：通过 Bazel `http_archive` 或 `local_repository` 引入 SNPE SDK 预编译库，在 `third_party/` 下提供 BUILD 文件
3. **平台条件编译**：SNPE 仅在 Linux aarch64 / Android 目标上可用，通过 `select()` 在非目标平台跳过编译
4. **清单配置扩展**：`backend: "snpe"`，`config` 字段传递 SNPE 专属参数（runtime target、performance profile 等）
5. **公共库集成**：`//src/public:atlas` 的 deps 通过 `select()` 在目标平台添加 SNPE 后端依赖

### 清单结构示例

```json
{
  "id": "detector",
  "backend": "snpe",
  "model_path": "${MODEL_DIR}/detector.dlc",
  "load_strategy": "eager",
  "inputs": [{
    "name": "images",
    "shape": [1, 3, 224, 224],
    "dtype": "float32",
    "layout": "NCHW"
  }],
  "outputs": [{"name": "scores", "shape": [1, 1000], "dtype": "float32"}],
  "config": {
    "runtime": "gpu",
    "performance_profile": "burst",
    "use_buffer": "true"
  }
}
```

**SNPE 专属 config 字段：**

| 字段 | 说明 | 可选值 |
|------|------|--------|
| `runtime` | 推理运行时目标 | `cpu` / `gpu` / `dsp` / `aip` |
| `performance_profile` | 性能模式 | `balanced` / `power_saver` / `sustained_high_performance` / `burst` |
| `use_buffer` | 是否使用 buffer 模式（而非 user tensor） | `true` / `false` |

### 后端接口实现

```cpp
namespace atlas {
namespace backend {

// SNPE shared runtime context — holds SNPE runtime environment
// (e.g. zdl::SNPE::Snpe, GPU/DSP/AIP runtime resources).
class SnpeBackendContext : public IBackendContext {
 public:
    SnpeBackendContext();
    ~SnpeBackendContext() override;

    std::string_view BackendType() const override;  // returns "snpe"

    // Initializes the SNPE runtime based on config (runtime target, etc.).
    utils::ErrorCode Init(const std::unordered_map<std::string,
                          std::string>& config);

 private:
    // SNPE runtime resources (zdl::SNPE::* handles)
    // ...
};

// SNPE inference backend — loads .dlc models and runs inference
// on Qualcomm DSP / GPU / AIP.
class SnpeBackend : public IBackend {
 public:
    SnpeBackend();
    ~SnpeBackend() override;

    utils::ErrorCode Load(const std::string& model_path,
                           const core::ModelConfig& config,
                           IBackendContext* ctx = nullptr) override;
    utils::ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                            std::vector<utils::Tensor>& outputs) override;
    std::vector<utils::TensorInfo> GetInputInfo() const override;
    std::vector<utils::TensorInfo> GetOutputInfo() const override;
    void Unload() override;
    bool IsLoaded() const override;

 private:
    SnpeBackendContext* active_ctx_ = nullptr;  // non-owning, borrowed from ModelManager
    // zdl::SNPE::Snpe* snpe_ = nullptr;  // SNPE network handle
    // ...
};

}  // namespace backend
}  // namespace atlas

ATLAS_REGISTER_BACKEND("snpe", atlas::backend::SnpeBackend)
ATLAS_REGISTER_BACKEND_CONTEXT("snpe", atlas::backend::SnpeBackendContext)
```

### 目录结构

```
src/backend/snpe/
├── snpe_backend.h
├── snpe_backend.cc
├── snpe_backend_context.h
├── snpe_backend_context.cc
└── BUILD

third_party/
└── snpe/
    └── snpe.BUILD          # BUILD file for SNPE SDK prebuilt library
```

### SNPE SDK 依赖引入方式

SNPE SDK 为闭源商业软件，无法通过 `http_archive` 下载，且体积较大不适合整个复制到 Bazel 工作区。采用 **条件编译 + stub 降级 + 环境变量符号链接** 方案：目标平台通过环境变量 `SNPE_SDK_PATH` 零拷贝引用 SDK 并实现完整推理；非目标平台编译 stub 实现，`Load()` 返回 `kBackendNotFound`，不引入 SDK 依赖。

**环境变量驱动的 Zero-Copy 仓库规则：**

通过环境变量 `SNPE_SDK_PATH` 指定 SNPE SDK 安装路径，自定义 Starlark repository rule 读取该变量并创建符号链接，Bazel 通过 `@snpe_sdk` 透明引用，无需复制文件。

```python
# third_party/snpe/snpe_repo.bzl

def _snpe_sdk_repo_impl(repository_ctx):
    snpe_path = repository_ctx.os.environ.get("SNPE_SDK_PATH", "")
    if snpe_path:
        repository_ctx.symlink(snpe_path, "snpe_sdk_root")

snpe_sdk_repo = repository_rule(
    implementation = _snpe_sdk_repo_impl,
    environ = ["SNPE_SDK_PATH"],
    local = True,
    doc = "Cfreates @snpe_sdk from $SNPE_SDK_PATH via symlink (zero-copy).",
)
```

**WORKSPACE 中声明（始终生效，无 SDK 时为空仓库）：**

```python
load("//third_party/snpe:snpe_repo.bzl", "snpe_sdk_repo")
snpe_sdk_repo(name = "snpe_sdk")
```

**`@snpe_sdk` 的 BUILD 文件（通过 build_file 参数引用）：**

```python
# third_party/snpe/snpe.BUILD
# build_file for @snpe_sdk — paths are relative to the symlinked root

cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/zdl/**/*.hpp"]),
    includes = ["snpe_sdk_root/include"],
    srcs = select({
        "//platforms:linux_aarch64": glob([
            "snpe_sdk_root/lib/aarch64-linux-gcc/*.so",
        ]),
        "//platforms:android_arm64": glob([
            "snpe_sdk_root/lib/aarch64-android-clang/*.so",
        ]),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
```

> `.so` glob 以实际 SNPE SDK 版本目录结构为准，实现时注释标注。

**使用方式（仅目标平台）：**

```bash
# Linux aarch64 / Android arm64 构建时设置环境变量：
SNPE_SDK_PATH=/opt/snpe-sdk bazel build --repo_env=SNPE_SDK_PATH //src/backend/snpe/...
```

macOS 上不设置该变量即可——`repository_rule` 创建空仓库，`select()` 落在 `//conditions:default: []`，不会引用任何 SDK 文件，stub 编译无影响。

**条件编译标记：**

通过 Bazel `defines` 控制编译路径。SNPE 完整实现在 Linux aarch64（嵌入式 Linux 边缘设备）和 Android arm64（Snapdragon 移动设备）两个目标平台启用：

```python
# src/backend/snpe/BUILD

SNPE_PLATFORM = select({
    "//platforms:linux_aarch64": ["ATLAS_SNPE_ENABLED=1"],
    "//platforms:android_arm64": ["ATLAS_SNPE_ENABLED=1"],
    "//conditions:default": [],
})
```

> **平台引用说明**：`//platforms:linux_aarch64` 和 `//platforms:android_arm64` 在 Proposal-003 / Proposal-004 中定义，替代 `@bazel_tools//src/conditions:*` 以统一 `select()` 与 `--platforms` 交叉编译标志。

**src/backend/snpe/BUILD 完整结构：**

```python
cc_library(
    name = "snpe_backend_context",
    srcs = ["snpe_backend_context.cc"],
    hdrs = ["snpe_backend_context.h"],
    defines = SNPE_PLATFORM,
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend_context",
        "//src/utils:types",
    ] + select({
        "//platforms:linux_aarch64": ["@snpe_sdk//:snpe"],
        "//platforms:android_arm64": ["@snpe_sdk//:snpe"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "snpe_backend",
    srcs = ["snpe_backend.cc"],
    hdrs = ["snpe_backend.h"],
    defines = SNPE_PLATFORM,
    deps = [
        ":snpe_backend_context",
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        "//src/core:manifest_config",
        "//src/utils:types",
    ] + select({
        "//platforms:linux_aarch64": ["@snpe_sdk//:snpe"],
        "//platforms:android_arm64": ["@snpe_sdk//:snpe"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
    visibility = ["//visibility:public"],
)
```

**源码中的条件编译：**

`snpe_backend.cc` 和 `snpe_backend_context.cc` 中通过 `#ifdef ATLAS_SNPE_ENABLED` 区分完整实现与 stub：

```cpp
// snpe_backend.cc

#ifdef ATLAS_SNPE_ENABLED
// === Full implementation (Linux aarch64 / Android arm64 + SNPE SDK) ===

#include "zdl/SNPE/SNPE.hpp"
// ... SNPE SDK includes ...

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                     const core::ModelConfig& config,
                                     IBackendContext* ctx) {
    // Real SNPE model loading: parse .dlc, create SNPE network,
    // configure runtime (cpu/gpu/dsp/aip), etc.
    // ...
    loaded_ = true;
    return utils::ErrorCode::kOk;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                      std::vector<utils::Tensor>& outputs) {
    // Real SNPE inference execution.
    // ...
    return utils::ErrorCode::kOk;
}

// ... other methods with real implementation ...

#else
// === Stub implementation (non-target platforms) ===
// No SNPE SDK dependency; compiles cleanly on macOS / Linux x86_64.

utils::ErrorCode SnpeBackend::Load(const std::string& model_path,
                                     const core::ModelConfig& config,
                                     IBackendContext* ctx) {
    return utils::ErrorCode::kBackendNotFound;
}

utils::ErrorCode SnpeBackend::Infer(const std::vector<utils::Tensor>& inputs,
                                      std::vector<utils::Tensor>& outputs) {
    return utils::ErrorCode::kNotInitialized;
}

std::vector<utils::TensorInfo> SnpeBackend::GetInputInfo() const { return {}; }
std::vector<utils::TensorInfo> SnpeBackend::GetOutputInfo() const { return {}; }
void SnpeBackend::Unload() {}
bool SnpeBackend::IsLoaded() const { return false; }

#endif
```

`snpe_backend_context.cc` 同理：

```cpp
// snpe_backend_context.cc

#ifdef ATLAS_SNPE_ENABLED
// Full implementation: create SNPE runtime, configure target, etc.
SnpeBackendContext::SnpeBackendContext() { /* ... */ }
std::string_view SnpeBackendContext::BackendType() const { return "snpe"; }
utils::ErrorCode SnpeBackendContext::Init(const std::unordered_map<
    std::string, std::string>& config) { /* ... */ return ErrorCode::kOk; }
#else
// Stub: no SNPE SDK, context is a no-op.
SnpeBackendContext::SnpeBackendContext() = default;
std::string_view SnpeBackendContext::BackendType() const { return "snpe"; }
utils::ErrorCode SnpeBackendContext::Init(const std::unordered_map<
    std::string, std::string>& config) { return ErrorCode::kBackendNotFound; }
#endif
```

**行为矩阵：**

| 平台 | `SNPE_SDK_PATH` | `ATLAS_SNPE_ENABLED` | SNPE SDK | 编译结果 | `Load()` 行为 |
|------|-----------------|----------------------|----------|----------|---------------|
| Linux aarch64（嵌入式 Linux） | 设置 | 定义 | `@snpe_sdk` 符号链接引用 | 完整实现 | 正常加载模型 |
| Android arm64（Snapdragon 移动设备） | 设置 | 定义 | `@snpe_sdk` 符号链接引用 | 完整实现 | 正常加载模型 |
| macOS（任意 CPU） | 不设置 | 未定义 | 不引入（空仓库） | stub 编译，无 SDK 依赖 | 返回 `kBackendNotFound` |
| Linux x86_64 | 不设置 | 未定义 | 不引入（空仓库） | stub 编译，无 SDK 依赖 | 返回 `kBackendNotFound` |

> **关键优势**：非目标平台开发者无需安装 SNPE SDK，`bazel build //...` 即可通过。后端注册始终生效（`ATLAS_REGISTER_BACKEND` 在 stub 分支也执行），`BackendFactory::Create("snpe")` 返回 stub 实例，调用 `Load()` 时才返回错误。目标平台通过环境变量零拷贝引用 SDK，无需复制大型文件。

### 公共库集成

`//src/public:atlas` 的 deps 添加 SNPE 后端（无需额外 `select()`，因为 SNPE 后端自身已通过 `SNPE_PLATFORM` 在非目标平台编译为 stub，始终可被依赖）：

```python
deps = [
    # ... existing deps ...
    "//src/backend/snpe:snpe_backend",
    "//src/backend/snpe:snpe_backend_context",
],
```

### 模型格式

SNPE 使用 `.dlc`（Deep Learning Container）格式模型。可通过 SNPE SDK 的 `snpe-onnx-to-dlc` 工具从 ONNX 转换。`ManifestParser` 的 `model_path` 字段直接指向 `.dlc` 文件路径，无需修改解析器。

### 交叉编译宿主要求

**编译（Compile）不强制要求 Linux 宿主**。C++ 头文件和目标 `.so` 均为平台无关内容——LLVM/Clang 交叉编译器配合 `--target=aarch64-linux-gnu` 或 `--target=aarch64-linux-android` 即可在 macOS / Windows 上产出目标平台二进制文件。Bazel 的 `--platforms` 交叉编译机制（Proposal-003 已建立）正是为这一场景设计的。

**但模型转换（Model Conversion）必须依赖 Linux x86_64 宿主**。SNPE SDK 官方仅提供 Linux x86_64 宿主版本，其内含的模型转换工具（`snpe-onnx-to-dlc`）和 Python 工具链均为 Linux ELF 二进制文件。这些工具负责将 ONNX 模型转换为 `.dlc` 格式并完成量化——若宿主非 Linux x86_64，工具链无法运行。

> **推荐实践**：在 Linux x86_64 宿主（或 Docker 容器 `ubuntu:22.04`）上完成 SNPE 集成的全流程开发。纯 C++ 编译步骤理论上不限于 Linux，但模型转换步骤必须依赖 Linux 环境，统一使用 Linux 宿主可避免环境割裂。

### SNPE SDK 版本与平台约束

| 维度 | 说明 |
|------|------|
| 宿主环境 | SDK 官方仅发布 Linux x86_64 版本 |
| 目标平台 | Linux aarch64（嵌入式）、Android aarch64（移动设备） |
| API 兼容性 | SDK 版本需与目标设备的 SNPE 运行时版本匹配（Qualcomm 无前向兼容保证） |
| 模型转换 | `snpe-onnx-to-dlc` 的运行需要 Linux x86_64 + Python 3.8/3.10 |

### SNPE SDK 按版本独立实现文件（替代 `#if` 条件分支）

#### 动机

当前 `snpe_backend.cc` 和 `snpe_backend_context.cc` 通过 `#if ATLAS_SNPE_VERSION_MAJOR` 宏在单个文件中混合 1.x 和 2.x 代码，`snpe_backend.cc` 内含 20+ 处条件编译分支。当 SNPE 3.x（或更高版本）出现时，`#if` 分支密度呈组合爆炸增长，代码可维护性急剧下降。

同样，`snpe.BUILD` 用双 glob pattern 同时尝试匹配两种目录布局（例如 `aarch64-oe-linux-gcc8.2/` OR `aarch64-linux-gcc*/`），一旦新版本 SDK 调整目录结构，glob 匹配容易失效。

**解决思路：每个 SNPE 主版本一个独立实现文件，消除文件内的版本条件编译。**

#### 文件拆分

```
src/backend/snpe/
├── snpe_backend.h                # 共享头文件（不变，SnpeImpl 前向声明）
├── snpe_backend_v1.cc            # SNPE 1.x 纯实现（零 ATLAS_SNPE_VERSION_MAJOR 分支）
├── snpe_backend_v2.cc            # SNPE 2.x 纯实现
├── snpe_backend_stub.cc          # 非目标平台 stub
├── snpe_backend_context.h        # 共享头文件（不变）
├── snpe_backend_context_v1.cc    # SNPE 1.x context 实现
├── snpe_backend_context_v2.cc    # SNPE 2.x context 实现
├── snpe_backend_context_stub.cc  # context stub
└── BUILD
```

每份 `_vN.cc` 完全内聚：只 include 自己版本的 SNPE 头文件，只使用自己版本的 API，零条件编译。`SnpeImpl` 在各自 `.cc` 中独立定义（PIMPL 模式天然支持，头文件中仅前向声明）。stub 文件同理——非目标平台的空实现独立为 `_stub.cc`，不与其他版本实现混合。

#### BUILD 版本选择

**零 `config_setting`、零 `--define`。** 版本由 `snpe_repo.bzl` 在加载阶段自动检测，通过 `@snpe_sdk//:version.bzl` 导出，BUILD 在 `load()` 阶段直接读取。

**`src/backend/snpe/BUILD`**（内联版本选择，无额外 `.bzl` 文件）：

```python
load("@snpe_sdk//:version.bzl", "SNPE_MAJOR")

SNPE_PLATFORM = select({
    "//platforms:linux_aarch64": ["ATLAS_SNPE_ENABLED=1"],
    "//platforms:android_arm64": ["ATLAS_SNPE_ENABLED=1"],
    "//conditions:default": [],
})

SNPE_SDK_DEP = select({
    "//platforms:linux_aarch64": ["@snpe_sdk//:snpe"],
    "//platforms:android_arm64": ["@snpe_sdk//:snpe"],
    "//conditions:default": [],
})

_BACKEND_V = (
    ["snpe_backend_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR
    else ["snpe_backend_stub.cc"]
)
_CONTEXT_V = (
    ["snpe_backend_context_v" + SNPE_MAJOR + ".cc"] if SNPE_MAJOR
    else ["snpe_backend_context_stub.cc"]
)

SNPE_BACKEND_SRC = select({
    "//platforms:linux_aarch64": _BACKEND_V,
    "//platforms:android_arm64": _BACKEND_V,
    "//conditions:default": ["snpe_backend_stub.cc"],
})

SNPE_CONTEXT_SRC = select({
    "//platforms:linux_aarch64": _CONTEXT_V,
    "//platforms:android_arm64": _CONTEXT_V,
    "//conditions:default": ["snpe_backend_context_stub.cc"],
})
```

构建时无需任何版本参数——版本由 SDK 自身决定：

```bash
# SDK 路径即版本声明
export SNPE_SDK_PATH=/opt/qcom/aistack/qairt/2.21.0.240401/
bazel build --config=linux_aarch64 //src/backend/snpe/...
```

#### `@snpe_sdk` 仓库规则：模板文件 + 属性驱动

每个版本一个独立的 BUILD 模板文件，`snpe_repo.bzl` 通过 `snpe_major` 属性选择对应模板，通过 `repository_ctx.template()` 渲染到 `@snpe_sdk`。

**`third_party/snpe/snpe_v2.BUILD`**（SNPE 2.21.0 验证）：

```python
cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/SNPE/**/*.hpp"]),
    includes = ["snpe_sdk_root/include/SNPE"],
    srcs = select({
        "@//platforms:linux_aarch64": glob([
            "snpe_sdk_root/lib/aarch64-oe-linux-gcc8.2/*.so",
        ]),
        "@//platforms:android_arm64": glob([
            "snpe_sdk_root/lib/aarch64-android/*.so",
        ]),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
```

**`third_party/snpe/snpe_v1.BUILD`**（SNPE 1.50.0 验证）：

```python
cc_library(
    name = "snpe",
    hdrs = glob(["snpe_sdk_root/include/zdl/**/*.hpp"]),
    includes = ["snpe_sdk_root/include/zdl"],
    srcs = select({
        "@//platforms:linux_aarch64": glob([
            "snpe_sdk_root/lib/aarch64-linux-gcc4.9/*.so",
        ]),
        "@//platforms:android_arm64": glob([
            "snpe_sdk_root/lib/aarch64-android-clang6.0/*.so",
        ]),
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
```

**`snpe_repo.bzl`**：

```python
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
        "snpe_major": attr.string(default = "2", values = ["1", "2"]),
        "snpe_sdk_path": attr.string(),
    },
    environ = ["SNPE_SDK_PATH"],
    local = True,
)
```

**WORKSPACE 中声明：**

```python
snpe_sdk_repo(
    name = "snpe_sdk",
    snpe_major = "2",           # 默认 2，可改为 "1"
    # snpe_sdk_path = "/path",  # 可选，不设则 fallback 到 $SNPE_SDK_PATH
)
```

> **关键特性**：版本和路径均为 WORKSPACE 属性，无需环境变量（也可通过 `$SNPE_SDK_PATH` 覆盖路径）。每个版本独立 BUILD 模板，新增版本只需添加对应模板文件。`--define` / `--config=snpe_vN` 全部消除。

#### 已确认的 SDK 参考路径

AI 开发时可直接引用以下路径分析 SDK 结构：

| 版本 | `ATLAS_SNPE_VERSION_MAJOR` | SDK 安装路径 |
|------|---------------------------|-------------|
| SNPE 2.21.0.240401 | `2` | `/opt/qcom/aistack/qairt/2.21.0.240401/` |
| SNPE 1.50.0.2622 | `1` | `/opt/qcom/sdk/snpe-1.50.0.2622/` |

> 开发者负责在对应版本开发前设置正确的 `SNPE_SDK_PATH` 指向上述路径（或新版 SDK 对应路径），AI 据此分析头文件与库文件结构。

#### AI 辅助分析新版本工作流

当需要接入 SNPE 新主版本（如 3.x）或验证现有版本路径时，AI 按以下步骤执行：

| 步骤 | 工具 | 操作 | 产出 |
|------|------|------|------|
| 1 | `list_files` | 递归扫描 `$SNPE_SDK_PATH/include/` | 确定头文件布局（`include/SNPE/` vs `include/zdl/` 等嵌套层次） |
| 2 | `list_files` | 递归扫描 `$SNPE_SDK_PATH/lib/` | 确定 `.so` 子目录名（如 `aarch64-oe-linux-gcc8.2/`、`aarch64-android/` 等） |
| 3 | `read_file` | 阅读关键头文件 | 识别 API 变化点 |
| 3a | — | `SNPE.hpp` / `SNPEBuilder.hpp` | 构造函数、`execute()` 签名、迭代器风格（`begin` vs `cbegin`） |
| 3b | — | `DlEnums.hpp` | Runtime 枚举（`CPU_FLOAT` vs `CPU_FLOAT32`）、PerformanceProfile 枚举新增项 |
| 3c | — | `SNPEFactory.hpp` | `initializeLogging()` 签名、TensorFactory 接口 |
| 3d | — | `IBufferAttributes.hpp` / `ITensor.hpp` | dtype 检测 API（`IOBufferDataType_t`）、tensor 迭代器声明 |
| 4 | `write_to_file` | 基于分析结果编写 | `snpe_backend_v{N}.cc`、`snpe_backend_context_v{N}.cc` |
| 5 | `write_to_file` | 基于目录结构编写 | `snpe_v{N}.BUILD`（模板文件） |
| 6 | `replace_in_file` | 更新 `snpe_repo.bzl` | `values` 列表新增 `"N"` |

#### 迁移路径

从当前单文件 `#if ATLAS_SNPE_VERSION_MAJOR` 模式迁移到按版本拆分：

1. 从 `snpe_backend.cc` 中提取 `#ifdef ATLAS_SNPE_ENABLED` 块内 1.x 路径代码 → 写入 `snpe_backend_v1.cc`
2. 从 `snpe_backend.cc` 中提取 `#ifdef ATLAS_SNPE_ENABLED` 块内 2.x 路径代码 → 写入 `snpe_backend_v2.cc`（默认版本）
3. 从 `snpe_backend.cc` 中提取 `#else` stub 代码 → 写入 `snpe_backend_stub.cc`
4. 对 `snpe_backend_context.cc` 重复上述拆分
5. 各 `_vN.cc` / `_stub.cc` 文件内不再出现 `ATLAS_SNPE_VERSION_MAJOR` 相关条件编译
6. 在 BUILD 中添加 `config_setting` + `select()`，移除 `defines` 中的 `ATLAS_SNPE_VERSION_MAJOR=1`
7. 拆分 `snpe.BUILD` 为 `snpe_v1.BUILD` / `snpe_v2.BUILD`

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 清单格式 | 无变更（`backend: "snpe"` 已被现有解析器支持） |
| 公共 API | 无变更（`ModelHandle::Run` 接口不变） |
| 内部模块 | 新增 `src/backend/snpe/`；`snpe_backend.cc` / `snpe_backend_context.cc` 拆分为 per-version `_v{N}.cc` + `_stub.cc` |
| 新增依赖 | SNPE SDK（闭源，local_repository 引入） |
| 平台支持 | Linux aarch64（嵌入式 Linux）+ Android arm64（Snapdragon 移动设备）；macOS / Linux x86_64 编译为 stub no-op |
| 公共库 | `//src/public:atlas` deps 增加条件依赖 |
| 预估工作量 | 中等（接口实现 + SDK 集成 + 条件编译 + 测试） |

## 四、后续动作

- [ ] 评审讨论（记录日期与结论）
- [ ] 阶段归属：
  - `→ phase2.md`（Feature 记录章节添加 Proposal-002 关联记录，因后端实现模式在阶段二建立）
- [ ] 若采纳：确认 SNPE SDK 版本与安装路径，按 `phase_spec.md` 流程推进实现
- [ ] 若驳回：填写驳回理由

---

> **【补充】** 2026-06-30 | 评审反馈，需在实现阶段澄清以下设计细节：

## 五、评审待澄清事项

### 5.1 `SnpeBackendContext::Init()` 调用链

当前 `IBackendContext` 基类（`src/backend/base/i_backend_context.h`）仅声明 `BackendType()`，无 `Init()` 虚函数。`CpuBackendContext` 在构造函数中完成初始化，不依赖外部 `Init()` 调用。

**设计决策（待确认）**：`SnpeBackendContext::Init()` 由 `SnpeBackend::Load()` 内部通过 `static_cast<SnpeBackendContext*>(ctx)` 调用。

**理由**：
- 符合现有模式——`CpuBackend::Load()` 同样通过 ctx 持有 `Ort::Env`，后端自行管理上下文生命周期
- 不修改 `IBackendContext` 基类，不产生接口变更
- `ModelManager::Init()` 中 `factory.CreateContext()` 仅创建实例，后续由首个 `Load()` 调用触发 `Init()`

**幂等性要求**：`Init()` 必须支持重复调用——同一 manifest 中多个 SNPE 模型依次 Load 时，`Init()` 在首个模型 Load 时完成 SDK 内部运行时初始化，后续调用应检测已初始化并跳过。

### 5.2 资源拆分：Context（共享）vs Backend（per-model）

**场景**：同一 manifest 中有两个 SNPE 模型，`config.runtime` 可能不同（如一个 `"dsp"` 一个 `"gpu"`）。

**现有架构约束**：`ModelManager::Init()` 中 `seen_types` set 保证每个后端类型只有一个 `IBackendContext` 实例。

**对照 `CpuBackendContext` 的资源拆分模式：**

| 资源 | `CpuBackendContext`（共享） | `CpuBackend`（per-model） |
|------|---------------------------|--------------------------|
| ONNX Runtime 环境 | `Ort::Env` | — |
| 推理会话 | — | `Ort::Session` |
| 内存分配器 | `Ort::Allocator`（可选） | — |
| Session options | — | per-model 线程数等 |

**SNPE 同样遵循此模式——可复用资源放入 Context：**

| 资源 | `SnpeBackendContext`（共享） | `SnpeBackend`（per-model） |
|------|-----------------------------|--------------------------|
| SNPE 日志/框架初始化 | `SNPEFactory::InitializeLogging()` | — |
| 平台运行时容器 | `zdl::DlSystem::Runtime_t` 容器 | — |
| user buffer 池（buffer 模式） | 跨模型复用的 tensor buffer 列表 | — |
| SNPE 网络实例 | — | `zdl::SNPE::SNPE` |
| runtime target（cpu/gpu/dsp/aip） | — | 从 `ModelConfig::config` 提取 |
| performance_profile | — | 从 `ModelConfig::config` 提取 |
| use_buffer | — | 从 `ModelConfig::config` 提取 |

**`config.runtime` 冲突问题**：`runtime` 不在 Context 层面消费——`SnpeBackendContext::Init()` 的 config 参数仅提取全局性字段（如日志级别）；per-model 字段（runtime / profile / use_buffer）绕开 Context，由 `SnpeBackend::Load()` 从 `ModelConfig::config` 直接提取，与 `Ort::SessionOptions` 的处理方式一致。

<bds-codeblock language="cpp">
// SnpeBackend::Load() pseudocode
ErrorCode SnpeBackend::Load(const std::string& model_path,
                              const ModelConfig& config,
                              IBackendContext* ctx) {
    // 1. 确保共享上下文初始化（全局一次性操作，幂等）
    if (ctx) {
        auto* snpe_ctx = static_cast<SnpeBackendContext*>(ctx);
        snpe_ctx->Init(config.config);  // 仅消费全局字段，per-model 字段被忽略
    }

    // 2. 从 ModelConfig::config 提取 per-model 参数，创建网络实例
    auto runtime = config.config.count("runtime")
                       ? ParseRuntime(config.config.at("runtime"))
                       : Runtime_t::GPU;  // 默认 GPU
    auto profile = config.config.count("performance_profile")
                       ? ParseProfile(config.config.at("performance_profile"))
                       : PerformanceProfile_t::BALANCED;

    snpe_ = SNPEBuilder(runtime, model_path)
                .setPerformanceProfile(profile)
                .build();  // per-model 拥有独立的 SNPE 网络实例
    // ...
}
</bds-codeblock>

### 5.3 `third_party/snpe/snpe.BUILD` 内容

SNPE SDK 典型目录结构（以 Linux aarch64 目标为例）：

```
snpe-sdk/
├── include/zdl/
│   ├── SNPE/SNPE.hpp
│   ├── SNPE/SNPEFactory.hpp
│   ├── DlSystem/DlSystem.hpp
│   └── ...
├── lib/
│   ├── aarch64-linux-gcc/
│   │   └── libSNPE.so
│   └── aarch64-android-clang/
│       └── libSNPE.so
└── lib/dsp/
    └── libsnpe_dsp_skel.so   # DSP 运行时需要同步部署到设备
```

**`third_party/snpe/snpe.BUILD` 关键结构**：

```python
cc_library(
    name = "snpe",
    hdrs = glob(["include/zdl/**/*.hpp"]),
    includes = ["include"],
    srcs = select({
        "//platforms:linux_aarch64": ["lib/aarch64-linux-gcc/libSNPE.so"],
        "//platforms:android_arm64": ["lib/aarch64-android-clang/libSNPE.so"],
    }),
    visibility = ["//visibility:public"],
)
```

> 具体路径映射（`aarch64-linux-gcc` vs `aarch64-android-clang` 子目录名）以实际 SNPE SDK 版本为准，实现阶段调整。

### 5.4 测试策略

| 平台 | 测试方式 | 覆盖内容 |
|------|----------|----------|
| macOS / Linux x86_64 | `bazel test //tests/backend/snpe/...` | stub 路径：验证 `Load()` 返回 `kBackendNotFound`、`Infer()` 返回 `kNotInitialized`、`GetInputInfo()` 返回空列表、`IsLoaded()` 返回 `false` |
| Linux aarch64 / Android arm64 | 手动验证（需连接开发板/设备） | 完整推理链路：加载 `.dlc` 模型 → `Infer()` → 验证输出形状与数值 |

**stub 测试文件**：`tests/backend/snpe/snpe_backend_test.cc`，参考 `tests/backend/cpu/cpu_backend_test.cc` 结构。

**stub 测试 BUILD 依赖条件**：`tests/backend/snpe/BUILD` 始终依赖 `//src/backend/snpe:snpe_backend`（stub 在非目标平台也可编译链接）。

### 5.5 线程安全

`SnpeBackend` 遵循 `IBackend` 接口声明——不要求线程安全。由 `ModelManager`（`model_manager.cc` 中的 `std::lock_guard<std::mutex>`）在外部保证串行化。

### 5.6 预处理冲突

SNPE `.dlc` 模型转换过程中可能内嵌标准化（mean/std）预处理。若清单中同时配置 `normalize` 字段，Pipeline 将自动插入 `NormalizeNode`，导致双重归一化。

**约定**：约束层面解决——清单中配置 SNPE 后端的模型条目不应声明 `normalize` 字段。此约束记录于 `docs/phase4.md`（阶段四实现文档），不通过代码强制校验。
---

> **【补充】** 2026-07-02 | v1 / v2 交叉编译实测结果，记录调试经验。

## 六、实测调试记录

### 6.1 验证环境

| 组件 | 信息 |
|------|------|
| NDK | r25b |
| Host | Linux x86_64 |
| 目标 | `--config=android_arm64` (ARM aarch64, API 24) |

### 6.2 通用问题（v1 & v2 共同）

#### 6.2.1 `SnpeImpl` 必须定义为嵌套类型

**现象**：`member access into incomplete type 'atlas::backend::SnpeBackend::SnpeImpl'`

**根因**：头文件 `snpe_backend.h` 中 `struct SnpeImpl;` 是 `SnpeBackend` 的嵌套前向声明。`.cc` 中以 `struct SnpeImpl { ... };` 定义的是另一个独立类型。

**修复**：所有实现文件中定义为 `struct SnpeBackend::SnpeImpl { ... };`。

#### 6.2.2 `Optional<T>::operator->()` 不存在

**现象**：`member reference type 'TensorShape' is not a pointer`

**根因**：v1 (1.50.0) 和 v2 (2.21.0) 的 `DlOptional<T>` 均不提供 `operator->()`，只提供 `operator*()` 和 `operator bool()`。

**修复**：`opt_shape->getDimensions()` → `(*opt_shape).getDimensions()`。

#### 6.2.3 `IOBufferDataType_t` 不存在

**现象**：`no member named 'IOBufferDataType_t' in namespace ...`

**根因**：v1 和 v2 均不包含 `IOBufferDataType_t` 类型。

**修复**：移除 `SnpeDtypeToAtlas()` / `SnpeElementByteSize()`，dtype 默认 `kFloat32`。

### 6.3 SNPE 1.50.0 专属问题

| 问题 | 现象 | 根因 | 修复 |
|------|------|------|------|
| 命名空间别名冲突 | `redefinition of 'DlSystem'` | 1.x SDK 中 `DlSystem` / `DlContainer` 已在全局作用域定义 | 使用 `zdl::SNPE::` / `zdl::DlSystem::` 全限定名 |
| 无日志 API | `no member 'terminateLogging'` | 1.50.0 的 `SNPEFactory` 无 `initializeLogging()` / `terminateLogging()` | `snpe_backend_context_v1.cc` 移除日志调用 |
| 无 `EXTREME_POWER_SAVER` | 枚举值不存在 | 1.50.0 的 `PerformanceProfile_t` 最大为 `LOW_BALANCED = 8` | 移除 `kPerfExtremePowerSaver` 常量及分支 |

### 6.4 SNPE 2.21.0 专属问题

| 问题 | 现象 | 根因 | 修复 |
|------|------|------|------|
| `.h` 头文件缺失 | `fatal error: 'DlSystem/DlError.h' file not found` | 2.21.0 同时有 `.h` 和 `.hpp` 头文件，`Wrapper.hpp` 引用 `.h` | `snpe_v2.BUILD` 的 `hdrs` 同时 glob `**/*.hpp` 和 `**/*.h` |

### 6.5 验证流程

新增版本或切换 SDK 时，按以下步骤验收：

| 步骤 | 检查项 | 预期结果 |
|------|--------|---------|
| 1 | `cat $(bazel info output_base)/external/snpe_sdk/version.bzl` | `SNPE_MAJOR = "1"` 或 `"2"` |
| 2 | `cat $(bazel info output_base)/external/snpe_sdk/BUILD.bazel \| head -3` | 对应版本模板注释头 |
| 3 | `bazel query 'deps(//src/backend/snpe:snpe_backend)' --output=build \| grep snpe_backend_v` | `android_arm64` 选中 `snpe_backend_v{N}.cc` |
| 4 | 交叉编译 `--config=android_arm64` | 零错误 |
| 5 | `file bazel-bin/.../libsnpe_backend.so` | `ELF 64-bit LSB shared object, ARM aarch64` |

---

## 七、开发环境配置

### 7.1 环境变量

在 shell 配置文件（`~/.bashrc`、`~/.zshrc`）中添加以下内容，根据目标 SNPE 版本取消对应注释：

```bash
# --- SNPE 2.x (默认，推荐) ---
# 方式一：source SDK 自带脚本（自动设置 SNPE_ROOT、PYTHONPATH、PATH 等）
source /opt/qcom/aistack/qairt/2.21.0.240401/bin/envsetup.sh
# 方式二：手动设置
# export SNPE_ROOT=/opt/qcom/aistack/qairt/2.21.0.240401

# --- SNPE 1.x ---
# 部分安装包可能存在 envsetup.sh 不可读/不可执行（权限受限）情况，
# 建议直接使用手动变量方式（见 7.3 命令模板）。
# export SNPE_ROOT=/opt/qcom/sdk/snpe-1.50.0.2622

export SNPE_SDK_PATH=$SNPE_ROOT
export PYTHONPATH=$SNPE_ROOT/lib/python
export PATH=$PATH:$SNPE_ROOT/bin/x86_64-linux-clang
export ANDROID_NDK_HOME=/opt/local/usr/android/ndk/25.2.9519653
```

> **说明**：
> - `SNPE_ROOT`：SNPE SDK 安装根目录，Bazel 构建时通过 `snpe_repo.bzl` 自动检测
> - `SNPE_SDK_PATH`：由 `SNPE_ROOT` 导出，`@snpe_sdk` 仓库规则读取此变量创建符号链接
> - `PYTHONPATH`：模型转换工具（`snpe-onnx-to-dlc`）的 Python 依赖路径
> - `PATH`：SNPE 命令行工具（模型转换、量化等）所在目录
> - `ANDROID_NDK_HOME`：交叉编译 Android arm64 目标所需的 NDK 路径

### 7.2 Python 虚拟环境

推荐使用 [uv](https://docs.astral.sh/uv/) 管理 Python 版本和虚拟环境。SNPE 模型转换工具依赖特定版本的 Python 及以下包：

| SNPE 版本 | Python 版本 | 创建与激活命令 |
|-----------|------------|---------------|
| 1.x（1.50.0） | 3.6 | `uv venv .venv36 --python 3.6 && source .venv36/bin/activate`（需 Ubuntu 18.04） |
| 2.x（2.21.0） | 3.8 | `uv venv .venv38 --python 3.8 && source .venv38/bin/activate` |

激活虚拟环境后安装依赖：

```bash
uv pip install onnx
uv pip install pyyaml
uv pip install packaging
```

> `.venv/`、`.venv36`、`.venv38` 均已加入 `.gitignore`，不会被提交到版本控制。

### 7.3 SNPE 1.x（1.50.0）验证命令（已实测）

以下命令用于 Linux x86_64 主机验证 V1 端到端链路（构建 + 推理）：

```bash
# 0) 工作区使用 SNPE v1
grep -n "snpe_major" WORKSPACE
# 预期: snpe_major = "1"

# 1) 构建示例
bazel build //examples/snpe_cpu:snpe_cpu

# 2) 运行示例（使用 V1 生成的 DLC）
export SAMPLE_MODEL_DIR=/tmp/snpe_sample_models_v1
./bazel-bin/examples/snpe_cpu/snpe_cpu examples/snpe_cpu/manifest.json
```

若需要在本机重新生成 V1 的 DLC（SNPE 1.50.0 转换器依赖 Python 3.6）：

```bash
# 3) 准备 Python 3.6 环境（一次性）
conda create -y -n snpe36 python=3.6
conda run -n snpe36 pip install onnx==1.10.2

# 4) 在 snpe36 环境中转换模型
conda run -n snpe36 bash -lc '
    export SNPE_ROOT=/opt/qcom/sdk/snpe-1.50.0.2622
    export SNPE_SDK_PATH=$SNPE_ROOT
    export PYTHONPATH=$SNPE_ROOT/lib/python
    export LD_LIBRARY_PATH=$SNPE_ROOT/lib/x86_64-linux-clang:$CONDA_PREFIX/lib:$LD_LIBRARY_PATH

    python examples/snpe_cpu/gen_models.py /tmp/snpe_sample_models_v1
'
```

常见问题：

- `libpython3.6m.so.1.0: not found`
    - 说明转换器运行在非 Python 3.6 环境；使用 `conda run -n snpe36 ...` 并补齐 `LD_LIBRARY_PATH=$CONDA_PREFIX/lib`。
- `libQnnHtp.so => not found`
    - 说明主机链接/运行时库集合不匹配；V1 x86_64 主机侧仅链接 `libSNPE.so` 可避免该依赖链路。
