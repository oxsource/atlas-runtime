# Proposal-002: SNPE 后端接入

> **提议日期**：2026-06-26
> **提议人**：pizzk <726676435@qq.com>
> **状态**：草案
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
└── snpe.BUILD              # BUILD file for SNPE SDK prebuilt library
```

### SNPE SDK 依赖引入方式

SNPE SDK 为闭源商业软件，无法通过 `http_archive` 下载。采用 **条件编译 + stub 降级** 方案：目标平台集成 SDK 并实现完整推理；非目标平台编译 stub 实现，`Load()` 返回 `kBackendNotFound`，不引入 SDK 依赖。

**条件编译标记：**

通过 Bazel `defines` 控制编译路径：

```python
# src/backend/snpe/BUILD

SNPE_PLATFORM = select({
    "@bazel_tools//src/conditions:linux_aarch64": ["ATLAS_SNPE_ENABLED=1"],
    "//conditions:default": [],
})

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
        "@bazel_tools//src/conditions:linux_aarch64": ["@snpe_sdk//:snpe"],
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
        "@bazel_tools//src/conditions:linux_aarch64": ["@snpe_sdk//:snpe"],
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
// === Full implementation (Linux aarch64 + SNPE SDK) ===

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

| 平台 | `ATLAS_SNPE_ENABLED` | SNPE SDK | 编译结果 | `Load()` 行为 |
|------|----------------------|----------|----------|---------------|
| Linux aarch64 | 定义 | `@snpe_sdk` 链接 | 完整实现 | 正常加载模型 |
| macOS | 未定义 | 不引入 | stub 编译，无 SDK 依赖 | 返回 `kBackendNotFound` |
| Linux x86_64 | 未定义 | 不引入 | stub 编译，无 SDK 依赖 | 返回 `kBackendNotFound` |

> **关键优势**：非目标平台开发者无需安装 SNPE SDK，`bazel build //...` 即可通过。后端注册始终生效（`ATLAS_REGISTER_BACKEND` 在 stub 分支也执行），`BackendFactory::Create("snpe")` 返回 stub 实例，调用 `Load()` 时才返回错误。

**WORKSPACE 中声明 local_repository（仅目标平台需要）：**

```python
# In WORKSPACE — uncomment and set path when building for Linux aarch64 with SNPE SDK
# local_repository(
#     name = "snpe_sdk",
#     path = "/path/to/snpe-sdk",
# )
```

### 公共库集成

`//src/public:atlas` 的 deps 添加 SNPE 后端：

```python
deps = [
    # ... existing deps ...
    "//src/backend/snpe:snpe_backend",
    "//src/backend/snpe:snpe_backend_context",
] + select({
    "@bazel_tools//src/conditions:linux_aarch64": [],
    "//conditions:default": [],
    # SNPE backend compiles to no-op on non-target platforms
}),
```

### 模型格式

SNPE 使用 `.dlc`（Deep Learning Container）格式模型。可通过 SNPE SDK 的 `snpe-onnx-to-dlc` 工具从 ONNX 转换。`ManifestParser` 的 `model_path` 字段直接指向 `.dlc` 文件路径，无需修改解析器。

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 清单格式 | 无变更（`backend: "snpe"` 已被现有解析器支持） |
| 公共 API | 无变更（`ModelHandle::Run` 接口不变） |
| 内部模块 | 新增 `src/backend/snpe/`，不修改现有模块 |
| 新增依赖 | SNPE SDK（闭源，local_repository 引入） |
| 平台支持 | Linux aarch64 / Android（macOS / Linux x86_64 上编译为 no-op） |
| 公共库 | `//src/public:atlas` deps 增加条件依赖 |
| 预估工作量 | 中等（接口实现 + SDK 集成 + 条件编译 + 测试） |

## 四、后续动作

- [ ] 评审讨论（记录日期与结论）
- [ ] 阶段归属：
  - `→ phase2.md`（Feature 记录章节添加 Proposal-002 关联记录，因后端实现模式在阶段二建立）
- [ ] 若采纳：确认 SNPE SDK 版本与安装路径，按 `phase_spec.md` 流程推进实现
- [ ] 若驳回：填写驳回理由
