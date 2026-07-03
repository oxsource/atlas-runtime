# 阶段三实现方案：ModelManager + Atlas API + 单元测试

> **文档版本**：1.0.1
> **对应代码版本**：v1.0.0
> **最后更新**：2026-07-03
> **状态**：已实现

## 一、目标与交付物

| 交付物 | 说明 |
|--------|------|
| `IBackendContext` 接口 | 每种后端类型共享的运行时上下文抽象 |
| `CpuBackendContext` | 持有 `Ort::Env`，供同类所有 `CpuBackend` 实例借用 |
| `IBackend::Load()` 签名更新 | 新增 `IBackendContext*` 参数（Phase 2 追加调整） |
| `CpuBackend` 重构 | 不再自建 `Ort::Env`，改为借用外部传入的 context |
| `ModelManager` | 解析清单 → 按后端类型创建共享 context → 按策略初始化后端实例 |
| `ModelHandle` | 轻量句柄，封装单次推理完整链路（Pipeline + Infer） |
| `AtlasRuntime` | 对外暴露的统一 C++ API 门面 |
| 清单 `load_strategy` 字段 | 支持 `eager`（预加载）/ `lazy`（首次使用时加载）|
| 单元测试 | 覆盖 Context 管理、ModelManager、ModelHandle、AtlasRuntime |

---

## 二、背景：为何需要共享运行时上下文

当一个清单中存在多个同类后端的模型时（如两个 `"backend": "cpu"` 的模型），当前
Phase 2 的设计会为每个 `CpuBackend` 实例各自创建一个 `Ort::Env`，存在两个问题：

1. **资源浪费**：`Ort::Env` 初始化代价高（线程池、日志系统等），多份冗余；
2. **不符合官方建议**：ONNX Runtime 明确建议整个进程只创建一个 `Ort::Env`，
   所有 `Session` 共享同一个 `Env`。

其他后端（TensorRT 的 `IExecutionContext`、RKNN 的 `rknn_context`）存在类似需求，
因此抽象出通用的 `IBackendContext` 接口，由 `ModelManager` 统一管理生命周期。

---

## 三、模块设计

### 3.1 整体关系

```
AtlasRuntime                        ← 对外公开，应用层直接使用
    │
    └── ModelManager                ← 内部管理器，不对外暴露
            │
            ├── ManifestParser      ← 已有（阶段一）
            ├── BackendFactory      ← 已有（阶段一，扩展 context 注册）
            ├── Pipeline            ← 已有（阶段二）
            │
            ├── IBackendContext["cpu"]   ← 新增，整个运行时唯一一份
            │       └── Ort::Env
            │
            └── ModelEntry[]        ← 每个模型的运行时状态
                    ├── CpuBackend  ← 借用 IBackendContext["cpu"]
                    └── Pipeline

ModelHandle                         ← GetModel() 返回，非拥有指针
    ├── ref → ModelEntry::backend
    └── ref → ModelEntry::pipeline
```

---

### 3.2 对象关系与所有权图

```
┌─────────────────────────────────────────────────────────────────────┐
│ ModelManager（拥有者）                                               │
│                                                                     │
│  contexts_                         entries_                         │
│  ┌──────────────────────────┐      ┌──────────────────────────────┐ │
│  │ "cpu" →                  │      │ "detector" →                 │ │
│  │  CpuBackendContext       │◄─ borrow ─ CpuBackend              │ │
│  │   └── Ort::Env (唯一)    │      │   Pipeline (detector)        │ │
│  │                          │      ├──────────────────────────────┤ │
│  │  (未来)                  │      │ "classifier" →               │ │
│  │ "tensorrt" →             │◄─ borrow ─ CpuBackend              │ │
│  │  TrtBackendContext       │      │   Pipeline (classifier)      │ │
│  └──────────────────────────┘      └──────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
         ▲
         │  owns (unique_ptr)
         │
  AtlasRuntime

                    ┌───────────────────────────────────┐
                    │ 应用层                             │
                    │                                   │
                    │  ModelHandle  ──────────────────► CpuBackend  │
                    │  (非拥有)     ──────────────────► Pipeline    │
                    │               ↑                               │
                    │          GetModel()                           │
                    │          AtlasRuntime                         │
                    └───────────────────────────────────────────────┘
```

**所有权规则：**

| 对象 | 拥有者 | 生命周期约束 |
|------|--------|-------------|
| `IBackendContext` | `ModelManager` | 与 `ModelManager` 同生死 |
| `IBackend`（每模型） | `ModelManager::ModelEntry` | 借用 context，context 必须先于 backend 销毁 |
| `Pipeline`（每模型）| `ModelManager::ModelEntry` | 独立 |
| `ModelHandle` | 调用方（栈或成员）| **不拥有**，生命周期必须短于 `AtlasRuntime` |

**接口层级：**

```
IBackendContext  ←── 抽象：每种后端类型共享的运行时资源
    └── CpuBackendContext（持有 Ort::Env）
    └── (未来) TrtBackendContext（持有 CUDA context）
    └── (未来) RknnBackendContext（持有 rknn_context）

IBackend         ←── 抽象：每个模型独立的推理实例
    └── CpuBackend（Session 借用 CpuBackendContext::GetEnv()）
    └── (未来) TrtBackend
    └── (未来) RknnBackend

ModelHandle      ←── 非抽象，仅持有 ModelEntry* 非拥有指针
                     封装 pipeline.Run() + backend.Infer() 调用链
```

---

### 3.2 IBackendContext（新增，`src/backend/base/`）

```cpp
namespace atlas {
namespace backend {

// Per-backend-type shared runtime resource.
// One instance is created per unique backend type string in a manifest
// and shared across all IBackend instances of that type.
// Lifetime is managed by ModelManager.
class IBackendContext {
 public:
    virtual ~IBackendContext() = default;
    // Returns the backend type string this context serves (e.g. "cpu").
    virtual std::string_view BackendType() const = 0;
};

}  // namespace backend
}  // namespace atlas
```

---

### 3.3 CpuBackendContext（新增，`src/backend/cpu/`）

```cpp
namespace atlas {
namespace backend {

// Shared ONNX Runtime environment for all CpuBackend instances.
// Ort::Env is constructed once and borrowed by each CpuBackend::Load().
class CpuBackendContext : public IBackendContext {
 public:
    CpuBackendContext();
    std::string_view BackendType() const override;
    Ort::Env& GetEnv();

 private:
    std::unique_ptr<Ort::Env> env_;
};

}  // namespace backend
}  // namespace atlas
```

---

### 3.4 IBackend::Load() 签名更新（Phase 2 追加调整）

```cpp
// Updated signature — ctx is optional for backward compatibility.
// Implementations should cast ctx to their concrete context type.
virtual utils::ErrorCode Load(const std::string& model_path,
                               const core::ModelConfig& config,
                               IBackendContext* ctx = nullptr) = 0;
```

`CpuBackend::Load()` 内部行为变化：
- 若 `ctx != nullptr`：从 `static_cast<CpuBackendContext*>(ctx)->GetEnv()` 借用 Env；
- 若 `ctx == nullptr`：回退到自建 `Ort::Env`（保持向后兼容，用于独立测试）。

---

### 3.5 BackendFactory 扩展（context 注册）

```cpp
// New type for context creator functions.
using BackendContextCreator = std::function<std::unique_ptr<IBackendContext>()>;

class BackendFactory {
 public:
    // Existing backend registration (unchanged).
    void Register(const std::string& name, BackendCreator creator);

    // Registers a context creator for |name|.  Called via
    // ATLAS_REGISTER_BACKEND_CONTEXT macro.
    void RegisterContext(const std::string& name,
                         BackendContextCreator creator);

    // Creates a shared context for |name|.  Returns nullptr if no
    // context creator is registered (backends without shared context).
    std::unique_ptr<IBackendContext> CreateContext(
        const std::string& name) const;

    // ... existing methods unchanged ...
};

#define ATLAS_REGISTER_BACKEND_CONTEXT(name, ctx_cls)             \
    ATLAS_REGISTER_BACKEND_CONTEXT_IMPL_(name, ctx_cls, __COUNTER__)
```

---

### 3.6 ModelManager（`src/core/`）

```cpp
namespace atlas {
namespace core {

enum class LoadStrategy { kEager = 0, kLazy };

struct ModelEntry {
    ModelConfig                        config;
    std::unique_ptr<backend::IBackend> backend;
    pipeline::Pipeline                 pipeline;
    LoadStrategy                       strategy = LoadStrategy::kEager;
    bool                               loaded   = false;
};

class ModelManager {
 public:
    // Initializes from a parsed manifest.
    // Creates one IBackendContext per unique backend type.
    // For kEager entries, calls IBackend::Load() immediately.
    utils::ErrorCode Init(const ManifestConfig& manifest);

    // Returns a non-owning pointer to the entry for |model_id|.
    // For kLazy entries not yet loaded, triggers Load() on first call.
    utils::ErrorCode GetEntry(const std::string& model_id,
                               ModelEntry** entry);

    void ReleaseAll();

 private:
    utils::ErrorCode EnsureLoaded(ModelEntry* entry);

    // One context per backend type string.
    std::unordered_map<std::string,
                       std::unique_ptr<backend::IBackendContext>> contexts_;
    std::unordered_map<std::string, ModelEntry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace core
}  // namespace atlas
```

**`Init()` 流程：**

```
1. 遍历 manifest.models，收集所有唯一 backend 类型；
2. 为每种类型调用 BackendFactory::CreateContext()，存入 contexts_；
3. 遍历 manifest.models：
   a. 通过 BackendFactory::Create() 创建 IBackend 实例；
   b. 根据 TensorInfo 调用 Pipeline::BuildInputPipeline()；
   c. 若 strategy == kEager，立即调用 backend->Load(path, cfg, ctx)；
4. 返回 kOk（若任意 eager 模型加载失败，返回错误并 ReleaseAll()）。
```

---

### 3.7 ModelHandle（`src/api/`）

```cpp
namespace atlas {
namespace api {

// Lightweight handle to a single model inference context.
// NOT thread-safe: do not share one handle across threads.
// Lifetime must not exceed the AtlasRuntime that produced it.
class ModelHandle {
 public:
    bool IsValid() const;

    // Full inference chain: Pipeline preprocessing → IBackend::Infer().
    utils::ErrorCode Run(const utils::Tensor& raw_input,
                          std::vector<utils::Tensor>* outputs);

    std::vector<utils::TensorInfo> GetInputInfo()  const;
    std::vector<utils::TensorInfo> GetOutputInfo() const;

 private:
    friend class AtlasRuntime;
    explicit ModelHandle(core::ModelEntry* entry);
    core::ModelEntry* entry_ = nullptr;  // non-owning
};

}  // namespace api
}  // namespace atlas
```

---

### 3.8 AtlasRuntime（`src/api/`）

```cpp
namespace atlas {
namespace api {

class AtlasRuntime {
 public:
    utils::ErrorCode Init(const std::string& manifest_path);
    ModelHandle      GetModel(const std::string& model_id);
    void             Release();
    bool             IsInitialized() const;

 private:
    core::ManifestParser               parser_;
    std::unique_ptr<core::ModelManager> manager_;
    bool                               initialized_ = false;
};

}  // namespace api
}  // namespace atlas
```

---

## 四、清单扩展：`load_strategy`

新增可选字段，不破坏现有清单兼容性，缺省为 `"eager"`：

```json
{
  "id": "detector",
  "backend": "cpu",
  "model_path": "/opt/models/yolo.onnx",
  "load_strategy": "lazy",
  "inputs": [...],
  "outputs": [...]
}
```

`ManifestParser` 新增常量：
```cpp
constexpr const char*      kKeyLoadStrategy    = "load_strategy";
constexpr std::string_view kLoadStrategyLazy   = "lazy";
constexpr std::string_view kLoadStrategyEager  = "eager";
```

---

## 五、新增/修改文件列表

```
src/
├── backend/
│   ├── base/
│   │   ├── i_backend.h            ← 修改：Load() 增加 ctx 参数
│   │   ├── i_backend_context.h    ← 新增
│   │   ├── backend_factory.h      ← 修改：增加 context 注册接口
│   │   ├── backend_factory.cc     ← 修改
│   │   └── BUILD                  ← 修改
│   └── cpu/
│       ├── cpu_backend.h          ← 修改：Load() 接受 IBackendContext*
│       ├── cpu_backend.cc         ← 修改：借用 CpuBackendContext::GetEnv()
│       ├── cpu_backend_context.h  ← 新增
│       ├── cpu_backend_context.cc ← 新增
│       └── BUILD                  ← 修改
├── core/
│   ├── manifest_config.h          ← 修改：ModelConfig 增加 load_strategy
│   ├── manifest_parser.cc         ← 修改：解析 load_strategy
│   ├── model_manager.h            ← 新增
│   ├── model_manager.cc           ← 新增
│   └── BUILD                      ← 修改
└── api/
    ├── atlas_runtime.h            ← 新增
    ├── atlas_runtime.cc           ← 新增
    ├── model_handle.h             ← 新增
    ├── model_handle.cc            ← 新增
    └── BUILD                      ← 新增
tests/
├── core/
│   ├── model_manager_test.cc      ← 新增
│   └── BUILD                      ← 修改
└── api/
    ├── atlas_runtime_test.cc      ← 新增
    └── BUILD                      ← 新增
```

---

## 六、单元测试覆盖点

### IBackendContext / CpuBackendContext

| 测试用例 | 期望结果 |
|----------|----------|
| `CpuBackendContext::BackendType()` | 返回 `"cpu"` |
| 同一 Env 被两个 CpuBackend 实例借用 | 两次推理均正常，Env 只初始化一次 |

### ModelManager

| 测试用例 | 期望结果 |
|----------|----------|
| 含两个 `cpu` 模型的清单初始化 | 仅创建一份 `CpuBackendContext` |
| eager 模型在 `Init()` 后已加载 | `entry->loaded == true` |
| lazy 模型首次 `GetEntry()` 触发加载 | 调用后 `entry->loaded == true` |
| 获取不存在的模型 ID | 返回 `kInvalidArgument` |
| eager 模型路径无效时 `Init()` 失败 | 返回错误，其他已加载模型一并 Release |
| `ReleaseAll()` 后所有 entry 已卸载 | `entry->loaded == false` |

### ModelHandle

| 测试用例 | 期望结果 |
|----------|----------|
| 默认构造句柄 `IsValid()` | 返回 `false` |
| 有效 entry 构造后 `IsValid()` | 返回 `true` |
| `Run()` identity 模型完整链路 | 输出值与输入一致 |
| `Run()` 无效句柄 | 返回 `kNotInitialized` |

### AtlasRuntime 集成

| 测试用例 | 期望结果 |
|----------|----------|
| `Init()` 合法清单（含 eager 模型）| 返回 `kOk` |
| `Init()` 不存在的文件 | 返回 `kFileNotFound` |
| `GetModel()` 存在的 ID | `IsValid() == true` |
| `GetModel()` 不存在的 ID | `IsValid() == false` |
| `GetModel()` 未调用 `Init()` | `IsValid() == false` |
| 端到端 `Init` → `GetModel` → `Run` | 输出正确 |
| `Release()` 后可重新 `Init()` | 第二次 `Init()` 返回 `kOk` |
| 清单含两个模型，分别获取并推理 | 两个 handle 均推理成功 |

---

## 七、阶段三不包含的内容

- 并发推理（`ModelHandle::Run()` 为单线程；并发实例化多个 handle 的线程安全池化）
- C 语言包装接口（为 FFI / Python 绑定预留，阶段五）
- 多模型串联流水线（后处理节点自动组合）
- 性能 benchmark（阶段五）
- TensorRT / RKNN / SNPE 后端（阶段四）

---

## Feature 记录

| Proposal | 日期 | 简述 | 状态 |
|----------|------|------|------|
| [Proposal-006](proposals/006-model-config-public-api.md) | 2026-06-30 | ModelHandle 暴露 ModelConfig 关键字段 | 已采纳 |
| [Proposal-007](proposals/007-third-party-dir-restructuring.md) | 2026-06-30 | 三方库目录按类型分文件夹管理 | 已采纳 |
| [Proposal-008](proposals/008-logger-implementation.md) | 2026-07-03 | 日志输出管理系统 | 已采纳 |
---

## Bugfix 记录

| BUG | 日期 | 简述 | 等级 | 状态 |
|-----|------|------|------|------|
| [BUG-001](bugfixes/BUG-001-missing-onnxruntime-linux-soname.md) | 2026-06-30 | Linux 平台 ONNX Runtime SONAME 缺失导致 5 个测试失败 | P0 | 已修复 |

---

> **【补充】** Proposal-006 | 2026-06-30 | ModelHandle 新增 GetBackend() / GetModelPath() / GetLoadStrategy() / GetConfig() 四个只读查询方法。详细设计见 docs/proposals/006-model-config-public-api.md。

> **【补充】** Proposal-007 | 2026-06-30 | third_party/ 按第三方库类型分目录管理（nlohmann_json/、onnxruntime/、snpe/），atlas_deps.bzl 同步更新 build_file Label。详细设计见 docs/proposals/007-third-party-dir-restructuring.md。

> **【补充】** Proposal-008 | 2026-07-03 | 文档版本 1.0.0 → 1.0.1。新增轻量级日志输出管理系统（src/utils/logger.h/cc），提供 ATLAS_LOGD/I/W/E 宏 + Logger 类双接口，支持跨平台自适应输出，零外部依赖。详细设计见 docs/proposals/008-logger-implementation.md。
