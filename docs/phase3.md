# 阶段三实现方案：ModelManager + Atlas API + 单元测试

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


## 一、目标与交付物

| 交付物 | 说明 |
|--------|------|
| `ModelManager` | 解析清单 → 按策略初始化后端 → 维护模型实例池 |
| `ModelHandle` | 轻量句柄，封装单次推理的完整链路（Pipeline + Infer） |
| `AtlasRuntime` | 对外暴露的统一 C++ API 门面 |
| 清单 `load_strategy` 字段 | 支持 `eager`（预加载）/ `lazy`（首次使用时加载）两种策略 |
| 单元测试 | 覆盖 ModelManager、ModelHandle、AtlasRuntime 的正常与异常路径 |

---

## 二、模块设计

### 2.1 整体关系

```
AtlasRuntime                        ← 对外公开，应用层直接使用
    │
    └── ModelManager                ← 内部管理器，不对外暴露
            │
            ├── ManifestParser      ← 已有（阶段一）
            ├── BackendFactory      ← 已有（阶段一）
            ├── Pipeline            ← 已有（阶段二）
            │
            └── ModelEntry[]        ← 每个模型的运行时状态
                    ├── IBackend    ← 已有（阶段一/二）
                    └── Pipeline    ← 已有（阶段二）

ModelHandle                         ← GetModel() 返回，生命周期由调用方持有
    ├── ref → IBackend
    └── ref → Pipeline
```

---

### 2.2 清单字段扩展：`load_strategy`

在每个 model 条目下新增可选字段，不影响现有清单的兼容性：

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

| 值 | 行为 |
|----|------|
| `"eager"`（默认） | `ModelManager::Init()` 时立即调用 `IBackend::Load()` |
| `"lazy"` | 第一次 `ModelHandle::Run()` 时才调用 `IBackend::Load()` |

**ManifestParser 需同步更新**：将 `load_strategy` 解析到 `ModelConfig` 的新字段 `load_strategy`。

---

### 2.3 ModelEntry（内部数据结构）

```cpp
namespace atlas {
namespace core {

enum class LoadStrategy {
    kEager = 0,
    kLazy,
};

// Runtime state for one model entry managed by ModelManager.
struct ModelEntry {
    ModelConfig                    config;
    std::unique_ptr<backend::IBackend> backend;
    pipeline::Pipeline             pipeline;
    LoadStrategy                   strategy = LoadStrategy::kEager;
    bool                           loaded   = false;
};

}  // namespace core
}  // namespace atlas
```

---

### 2.4 ModelManager

**文件：** `src/core/model_manager.h / .cc`

```cpp
namespace atlas {
namespace core {

class ModelManager {
 public:
    ModelManager() = default;
    ~ModelManager();

    // Initializes from a parsed manifest.  For kEager models, Load() is
    // called immediately.  Returns kOk only if all eager models load
    // successfully.
    utils::ErrorCode Init(const ManifestConfig& manifest);

    // Returns a non-owning pointer to the ModelEntry for |model_id|.
    // Returns nullptr if the id is not found.
    // For kLazy entries that have not been loaded yet, triggers Load()
    // on first access.
    utils::ErrorCode GetEntry(const std::string& model_id,
                               ModelEntry** entry);

    // Unloads all backends and clears internal state.
    void ReleaseAll();

 private:
    // Loads the backend for |entry| if not already loaded.
    utils::ErrorCode EnsureLoaded(ModelEntry* entry);

    std::unordered_map<std::string, ModelEntry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace core
}  // namespace atlas
```

**关键设计决策：**

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 线程安全粒度 | `std::mutex` 保护 `entries_` 读写 | Phase 3 保证 ModelManager 接口线程安全即可 |
| `ModelHandle::Run()` 线程安全 | **不保证**（单句柄单线程使用） | 嵌入式场景通常单线程推理；并发池化在后续版本扩展 |
| Entry 所有权 | `ModelManager` 独占，`ModelHandle` 持裸指针 | ModelHandle 生命周期必须短于 AtlasRuntime |

---

### 2.5 ModelHandle

**文件：** `src/api/model_handle.h / .cc`

```cpp
namespace atlas {
namespace api {

// Lightweight handle to a single model's inference context.
// Lifetime must not exceed the AtlasRuntime that produced it.
// Not thread-safe: do not call Run() concurrently on the same handle.
class ModelHandle {
 public:
    // Returns true if this handle references a valid, loaded model.
    bool IsValid() const;

    // Runs the full inference chain:
    //   raw_input  →  Pipeline (preprocessing)  →  IBackend::Infer()
    //              →  outputs
    //
    // |raw_input|  : source tensor (e.g. HWC uint8 image).
    // |outputs|    : populated with inference results on success.
    utils::ErrorCode Run(const utils::Tensor& raw_input,
                          std::vector<utils::Tensor>* outputs);

    // Returns input/output metadata from the underlying backend.
    std::vector<utils::TensorInfo> GetInputInfo()  const;
    std::vector<utils::TensorInfo> GetOutputInfo() const;

 private:
    friend class AtlasRuntime;
    // Only AtlasRuntime can construct valid handles.
    ModelHandle(core::ModelEntry* entry);

    core::ModelEntry* entry_ = nullptr;   // non-owning
};

}  // namespace api
}  // namespace atlas
```

**`Run()` 内部流程：**

```
1. EnsureLoaded() (for lazy entries — delegate to ModelManager)
2. pipeline_.Run(raw_input, &preprocessed)
3. backend_->Infer({preprocessed}, outputs)
```

---

### 2.6 AtlasRuntime

**文件：** `src/api/atlas_runtime.h / .cc`

```cpp
namespace atlas {
namespace api {

// Top-level facade exposed to application code.
// Typical usage:
//
//   AtlasRuntime rt;
//   rt.Init("manifest.json");
//   ModelHandle h = rt.GetModel("detector");
//   h.Run(image_tensor, &outputs);
//   rt.Release();
class AtlasRuntime {
 public:
    AtlasRuntime() = default;
    ~AtlasRuntime();

    // Parses |manifest_path| and initializes all eager models.
    // Must be called before GetModel().
    utils::ErrorCode Init(const std::string& manifest_path);

    // Returns a ModelHandle for |model_id|.
    // Returns an invalid handle (IsValid() == false) if id not found or
    // Init() has not been called.
    ModelHandle GetModel(const std::string& model_id);

    // Unloads all models and releases resources.  After Release(),
    // Init() may be called again.
    void Release();

    bool IsInitialized() const { return initialized_; }

 private:
    core::ManifestParser               parser_;
    std::unique_ptr<core::ModelManager> manager_;
    bool                               initialized_ = false;
};

}  // namespace api
}  // namespace atlas
```

---

## 三、新增文件列表

```
atlas/
└── src/
    ├── core/
    │   ├── model_manager.h        ← 新增
    │   ├── model_manager.cc       ← 新增
    │   └── BUILD                  ← 更新（添加 model_manager 目标）
    └── api/
        ├── atlas_runtime.h        ← 新增
        ├── atlas_runtime.cc       ← 新增
        ├── model_handle.h         ← 新增
        ├── model_handle.cc        ← 新增
        └── BUILD                  ← 新增
tests/
    ├── core/
    │   ├── model_manager_test.cc  ← 新增
    │   └── BUILD                  ← 更新
    └── api/
        ├── atlas_runtime_test.cc  ← 新增
        └── BUILD                  ← 新增
```

同时更新：
- `src/core/manifest_config.h`：`ModelConfig` 新增 `load_strategy` 字段
- `src/core/manifest_parser.cc`：解析 `load_strategy` 字段

---

## 四、ManifestConfig 变更

```cpp
// Added to ModelConfig in manifest_config.h:
enum class LoadStrategy { kEager = 0, kLazy };

struct ModelConfig {
    std::string id;
    std::string name;
    std::string backend;
    std::string model_path;
    LoadStrategy load_strategy = LoadStrategy::kEager;   // ← 新增
    std::vector<utils::TensorInfo> inputs;
    std::vector<utils::TensorInfo> outputs;
    std::unordered_map<std::string, std::string> config;
};
```

ManifestParser 中新增常量与解析逻辑（对应 code_spec 禁止魔术字符串规范）：
```cpp
constexpr std::string_view kKeyLoadStrategy = "load_strategy";
constexpr std::string_view kLoadStrategyLazy = "lazy";
```

---

## 五、单元测试覆盖点

### ModelManager 测试

| 测试用例 | 期望结果 |
|----------|----------|
| 使用含一个 eager 模型的清单初始化 | `Init()` 返回 `kOk`，模型已加载 |
| 获取存在的模型条目 | `GetEntry()` 返回非空指针 |
| 获取不存在的模型 ID | 返回 `kInvalidArgument` |
| lazy 模型在 `GetEntry()` 时触发加载 | 首次访问后 `entry->loaded == true` |
| 初始化后调用 `ReleaseAll()` | 所有 `entry->loaded == false` |
| eager 模型路径不存在时 Init 失败 | 返回 `kInvalidArgument` 或 `kFileNotFound` |

### ModelHandle 测试

| 测试用例 | 期望结果 |
|----------|----------|
| 默认构造的 handle `IsValid()` | 返回 `false` |
| 从合法 entry 构造后 `IsValid()` | 返回 `true` |
| `Run()` 完整推理链路（identity 模型）| 返回 `kOk`，输出形状与输入一致 |
| `Run()` 使用无效 handle | 返回 `kNotInitialized` |

### AtlasRuntime 集成测试

| 测试用例 | 期望结果 |
|----------|----------|
| `Init()` 合法清单文件 | 返回 `kOk` |
| `Init()` 不存在的清单文件 | 返回 `kFileNotFound` |
| `GetModel()` 存在的 ID | 返回 `IsValid() == true` 的 handle |
| `GetModel()` 不存在的 ID | 返回 `IsValid() == false` 的 handle |
| `GetModel()` 未调用 `Init()` | 返回无效 handle |
| 端到端：`Init` → `GetModel` → `Run`（identity 模型）| 输出值与输入一致 |
| `Release()` 后再次 `Init()` | 可正常重新初始化 |

---

## 六、阶段三不包含的内容

- 并发推理 / 线程池（`ModelHandle::Run()` 为单线程）
- 多模型串联流水线（后处理节点的自动组合）
- C 语言包装接口（为后续 FFI / Python 绑定预留）
- 性能 benchmark（阶段五）
- TensorRT / RKNN / SNPE 后端（阶段四）
