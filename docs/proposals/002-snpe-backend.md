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
└── snpe/
    └── snpe.BUILD          # BUILD file for SNPE SDK prebuilt library
```

### SNPE SDK 依赖引入方式

SNPE SDK 为闭源商业软件，无法通过 `http_archive` 下载。采用 **条件编译 + stub 降级** 方案：目标平台集成 SDK 并实现完整推理；非目标平台编译 stub 实现，`Load()` 返回 `kBackendNotFound`，不引入 SDK 依赖。

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
| Linux aarch64（嵌入式 Linux） | 定义 | `@snpe_sdk` 链接（`aarch64-linux-gcc` 目标库） | 完整实现 | 正常加载模型 |
| Android arm64（Snapdragon 移动设备） | 定义 | `@snpe_sdk` 链接（`aarch64-android-clang` 目标库） | 完整实现 | 正常加载模型 |
| macOS（任意 CPU） | 未定义 | 不引入 | stub 编译，无 SDK 依赖 | 返回 `kBackendNotFound` |
| Linux x86_64 | 未定义 | 不引入 | stub 编译，无 SDK 依赖 | 返回 `kBackendNotFound` |

> **关键优势**：非目标平台开发者无需安装 SNPE SDK，`bazel build //...` 即可通过。后端注册始终生效（`ATLAS_REGISTER_BACKEND` 在 stub 分支也执行），`BackendFactory::Create("snpe")` 返回 stub 实例，调用 `Load()` 时才返回错误。

**WORKSPACE 中声明 local_repository（仅目标平台需要）：**

```python
# In WORKSPACE — uncomment and set path when building for target platforms with SNPE SDK.
# Linux aarch64（嵌入式 Linux 边缘设备）：指向 SDK 根目录。
# Android arm64（Snapdragon 移动设备）：需额外配置 Android NDK 工具链（见 Proposal-004），
#   并确保 local_repository 指向的路径下包含 aarch64-android-clang 目标库。
# local_repository(
#     name = "snpe_sdk",
#     path = "/path/to/snpe-sdk",
# )
```

> **Android 目标库路径说明**：SNPE SDK 为 Linux aarch64 嵌入式（`aarch64-linux-gcc`）和 Android（`aarch64-android-clang`）提供不同的预编译 `.so`。`@snpe_sdk` 的 BUILD 文件需通过 `select()` 根据目标平台选择正确的库路径。具体路径映射在实现阶段根据实际 SNPE SDK 版本确定。

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

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 清单格式 | 无变更（`backend: "snpe"` 已被现有解析器支持） |
| 公共 API | 无变更（`ModelHandle::Run` 接口不变） |
| 内部模块 | 新增 `src/backend/snpe/`，不修改现有模块 |
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