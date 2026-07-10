# Proposal-001: Manifest 自由配置 Pipeline

> **提议日期**：2026-06-26（初稿）
> **更新日期**：2026-07-10（修订：移除自动构建，开放自定义节点注册，增加分阶段 Profiling；补充 `IPipelineNode::Context` 上下文 + 节点命名空间 + `IBackend*` + `flags/args` 扩展机制）
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：阶段级
> **关联**：`docs/phase2.md` 已知局限性、`docs/architecture.md` 第 3.5 节 Pipeline

## 一、背景

### 1.1 原始设计及问题

当前 Atlas 的 Pipeline 预处理链由 `Pipeline::BuildInputPipeline()` 根据 `TensorInfo` 自动构建，节点顺序固定为：

```
DtypeConvert → Resize → BGRToRGB → HWCToCHW → Normalize
```

该设计在阶段二实现时满足了基础需求，但在实际使用中存在以下局限性：

1. **节点顺序固定**：用户无法在清单中调整节点顺序或跳过某个节点；
2. **仅支持 inputs[0]**：多输入模型只构建第一个输入的预处理管线；
3. **无自定义节点清单配置**：清单未提供声明式字段；
4. **无输出后处理管线**：未实现 `BuildOutputPipeline`。

### 1.2 修订背景（2026-07-10）

经过阶段四/五的实际验证，发现 Pipeline 的设计还存在更深层次的结构性问题：

| 问题 | 说明 |
|------|------|
| **自动构建逻辑不可靠** | `BuildInputPipeline()` 自动推断的节点链与模型真实需求不匹配，且节点只接受 3-D HWC 输入，与模型的 4-D NCHW 输入冲突 |
| **`disable_pipeline` 成了常态** | 引入 `disable_pipeline` 作为逃生舱，但实际成了唯一能工作的配置，SNPE 等后端通过 `GetInputTensor()`/`GetOutputBuffer()` 零拷贝路径的数据根本不需要经过管线 |
| **管线不可扩展** | 内置节点固定，用户无法注册自定义预处理/后处理节点 |
| **Profile 无分层统计** | `ProfilingBackend` 只统计了 `Infer()` 耗时，操作管线的耗时完全不可见 |

本次修订的核心方向：**去掉自动构建，保留显式配置，开放自定义注册，增加分层 Profile**。

## 二、方案概要

在 manifest 的 `inputs[]` 和 `outputs[]` 中新增可选的 `pipeline` 字段，允许用户以声明式方式定义预处理 / 后处理节点链。**删除 `BuildInputPipeline()` 自动构建和 `disable_pipeline` 字段**，管线行为完全由用户显式配置决定。

> **唯一性约束**：同一 `pipeline` 数组内的节点 `name` 必须唯一，`ManifestParser` 在解析时检测重复并返回 `kParseError`。
>
> **显示声明即唯一配置方式**：不写 `pipeline` 数组 = 无预处理（identity 直通）。

核心思路：

1. **清单扩展**：在 `TensorInfo` 中新增 `pipeline` 数组字段，每个元素描述一个节点（名称 + 参数），**同一 pipeline 内节点 `name` 必须唯一**；
2. **节点注册表**：引入 `PipelineNodeFactory`，将内置节点（DtypeConvert / Resize / BGRToRGB / HWCToCHW / Normalize）注册为可通过字符串名称创建；
3. **用户自定义节点**：开放 `ATLAS_REGISTER_PIPELINE_NODE` 宏，允许用户在代码中注册任意自定义节点，manifest 中按 name 引用；
4. **无自动构建**：删除 `BuildInputPipeline()`，`pipeline` 数组为空时不构建任何管线；
5. **多输入支持**：`ModelManager` 为每个 input 构建独立的 Pipeline；
6. **分阶段 Profile**：`ModelHandle::Run()` 中对 `input_pipeline` / `forward` / `output_pipeline` 三段分别计时，使用与 `ProfilingBackend` 相同的 `ProfileRecord` 结构体；
7. **`IPipelineNode::Context` 上下文传递**：`Process()` 参数由 `PipelineContext` 改为 `IPipelineNode::Context`，作为嵌套类型，携带 `config`、`backend`、`input_index`、`output_index`、`flags`、`args` 等上下文信息；
8. **节点命名空间**：内置节点使用 `atlas::` 前缀注册，用户节点使用自定义前缀（如 `mycompany::`），防止命名冲突；
9. **节点文档**：在 `src/pipeline/nodes/` 下新增节点说明文档。

### 清单结构示例

**预处理（inputs）自定义节点链：**

```json
{
  "inputs": [
    {
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "pipeline": [
        { "name": "atlas::dtype_convert", "params": { "target": "float32" } },
        { "name": "atlas::resize", "params": { "height": 224, "width": 224 } },
        { "name": "atlas::bgr_to_rgb" },
        { "name": "atlas::hwc_to_chw" },
        { "name": "atlas::normalize", "params": { "mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225] } }
      ]
    }
  ]
}
```

**后处理（outputs）自定义节点链：**

```json
{
  "outputs": [
    {
      "name": "scores",
      "shape": [1, 1000],
      "dtype": "float32",
      "pipeline": [
        { "name": "atlas::softmax" },
        { "name": "atlas::topk", "params": { "k": 5 } }
      ]
    }
  ]
}
```

**无 pipeline 字段（无预处理，identity 直通）：**

```json
{
  "inputs": [
    {
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW"
    }
  ]
}
```

**用户自定义节点（通过 ATLAS_REGISTER_PIPELINE_NODE 注册）：**

```json
{
  "inputs": [
    {
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "pipeline": [
        { "name": "mycompany::augment", "params": { "amount": "0.5", "method": "gaussian" } },
        { "name": "atlas::dtype_convert", "params": { "target": "float32" } },
        { "name": "atlas::normalize", "params": { "mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225] } }
      ]
    }
  ]
}
```

### 节点注册表结构示例

```cpp
// 按字符串名称创建节点实例
class PipelineNodeFactory {
 public:
    static PipelineNodeFactory& Instance();
    void Register(const std::string& name, NodeCreator creator);
    std::unique_ptr<IPipelineNode> Create(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& params) const;

    // 列出所有已注册的节点名称（用于诊断/调试）
    std::vector<std::string> ListNodeNames() const;
};

// 内置节点注册（使用 atlas:: 前缀）
ATLAS_REGISTER_PIPELINE_NODE("atlas::resize", ResizeNode)
ATLAS_REGISTER_PIPELINE_NODE("atlas::normalize", NormalizeNode)
ATLAS_REGISTER_PIPELINE_NODE("atlas::softmax", SoftmaxNode)
// ...

// 用户自定义节点注册（使用自定义前缀，如 mycompany::）
class MyAugmentNode : public IPipelineNode {
    utils::ErrorCode Process(const Context& ctx,
                              utils::Tensor* output,
                              const Context& ctx) override;

    std::string_view Name() const override { return "mycompany::augment"; }

    // 工厂方法：PipelineNodeFactory 通过此方法创建节点实例
    static std::unique_ptr<IPipelineNode> CreateFromParams(
        const std::unordered_map<std::string, std::string>& params);
};
ATLAS_REGISTER_PIPELINE_NODE("mycompany::augment", MyAugmentNode)
```

> **命名规范**：节点名称使用 `namespace::name` 格式，`::` 作为命名空间分隔符。内置节点统一使用 `atlas::` 前缀，用户自定义节点应使用自有前缀（如 `mycompany::`、`myproject::`），避免命名冲突。
>
> `ATLAS_REGISTER_PIPELINE_NODE` 使用的是编译期静态注册，注册在任何 `main()` 或 `AtlasRuntime::Init()` 之前生效。用户自定义节点无需修改框架代码。

### `IPipelineNode::Context` 上下文传递

链式调用中，节点常需要访问模型配置、后端输入缓冲区、当前处理的 input/output 索引等上下文信息。上下文结构体定义为 `IPipelineNode` 的嵌套类型 `Context`，由 `Pipeline::Run()` 在遍历节点链时传入：

```cpp
// ──── 执行上下文 Flags ──────────────────────────────────
constexpr uint8_t kPipeFlagNone       = 0x00;  // 无特殊标记
constexpr uint8_t kPipeFlagInputPipe  = 0x01;  // 运行在 input pipeline 阶段
constexpr uint8_t kPipeFlagOutputPipe = 0x02;  // 运行在 output pipeline 阶段
// Bits 2-7: 保留给用户自定义 flag

// ──── IPipelineNode 定义，Context 作为嵌套类型 ──────────
class IPipelineNode {
 public:
    // 节点执行上下文，Pipeline::Run() 遍历节点链时传入每个 Process()
    struct Context {
        const core::ModelConfig*  config   = nullptr;  // 模型 manifest 配置
        const backend::IBackend*  backend  = nullptr;  // 后端，用于零拷贝访问输入缓冲区
        size_t                    input_index  = 0;  // Current input index
        size_t                    output_index = 0;  // Current output index
        uint8_t                   flags    = 0;        // 位掩码，见 kPipeFlag*
        void*                     args     = nullptr;  // 用户自定义扩展数据
    };

    virtual ~IPipelineNode() = default;

    virtual utils::ErrorCode Process(const Context& ctx,
                                      const utils::Tensor& input,
                                      utils::Tensor* output) = 0;

    virtual std::string_view Name() const = 0;
    virtual bool SupportsInPlace() const { return false; }
};
```

典型使用场景：

| 场景 | Context 字段 | 说明 |
|------|-------------|------|
| **多输入差异化处理** | `ctx.input_index` | 对 inputs[0] 做 BGR→RGB，对 inputs[1] 不做通道交换 |
| **零拷贝写入后端 buffer** | `ctx.backend->GetInputBuffer(ctx.input_index)` | YUV→RGB 节点直接写入后端输入缓冲区，避免多余 memcpy |
| **读取模型级配置** | `ctx.config->config["mean"]` | 节点从 manifest config 读取自定义参数 |
| **后处理依赖模型输出结构** | `ctx.output_index` | 不同输出使用不同的后处理策略（如 NMS vs Softmax） |
| **扩展数据传递** | `ctx.args` | 节点间传递自定义数据对象，`nullptr` 表示无扩展 |

节点内使用示例（YUV→RGB 零拷贝）：

```cpp
utils::ErrorCode MyYuvToRgbNode::Process(const Context& ctx,
                                           const Tensor& input,
                                           Tensor* output) override {
    // 获取后端输入缓冲区（零拷贝目标）
    auto buf = ctx.backend->GetInputBuffer(ctx.input_index);

    // YUV → RGB，直接写入 backend buffer
    YuvToRgb(input.data, buf.data, /* width */, /* height */);

    // 让 output 指向 backend buffer，后续 Infer() 零拷贝
    output->data      = buf.data;
    output->byte_size = buf.size;
    output->owns_data = false;
    output->info      = ctx.backend->GetInputInfoAt(ctx.input_index);

    return utils::ErrorCode::kOk;
}
```

`Pipeline::Run()` 内部实现：

```cpp
utils::ErrorCode Pipeline::Run(const utils::Tensor& input,
                                utils::Tensor* output,
                                const IPipelineNode::Context& ctx) const {
    const Tensor* current = &input;
    Tensor scratch;
    for (const auto& node : nodes_) {
        Tensor* next = (node == nodes_.back()) ? output : &scratch;
        auto ret = node->Process(ctx, *current, next);
        if (ret != utils::ErrorCode::kOk) return ret;
        current = next;
    }
    return utils::ErrorCode::kOk;
}
```

### 节点命名空间（Namespace）

**设计动机**：内置节点和用户自定义节点共用同一个 `PipelineNodeFactory` 注册表，若不引入命名空间，用户无意中注册了与框架同名的节点会导致冲突。

**命名规则**：

| 类别 | 命名格式 | 示例 | 说明 |
|------|---------|------|------|
| 内置节点 | `atlas::name` | `atlas::resize` | Atlas 框架内置全部使用 `atlas::` 前缀 |
| 用户自定义 | `project::name` | `mycompany::nms` | 用户按自己的项目/组织命名 |
| 第三方库 | `libname::name` | `opencv::warp_affine` | 第三方集成库使用库名前缀 |

**解析优先级**：`PipelineNodeFactory::Create()` 查找时，先尝试完整名称（如 `mycompany::augment`），若未命中则尝试无前缀名称（如 `augment`）作为回退，保持简写兼容性：

```cpp
std::unique_ptr<IPipelineNode> PipelineNodeFactory::Create(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& params) const {
    // 1. 精确匹配
    auto it = creators_.find(name);
    if (it != creators_.end()) return it->second(params);

    // 2. 若包含 ::，不降级（防止意外匹配）
    if (name.find("::") != std::string::npos) return nullptr;

    // 3. 无 :: 时，尝试添加 atlas:: 前缀回退
    it = creators_.find("atlas::" + name);
    if (it != creators_.end()) return it->second(params);

    return nullptr;
}
```

**manifest 中的简写支持**：用户可在 manifest 中省略 `atlas::` 前缀，解析器自动补全：

```json
{ "name": "resize" }                    // 自动解析为 atlas::resize
{ "name": "atlas::resize" }             // 显式全称，同上
{ "name": "mycompany::augment" }        // 自定义节点必须全称
```

> **兼容性**：现有 manifest 中所有无前缀的节点名称（如 `"resize"`、`"normalize"`）通过回退逻辑自动映射到 `atlas::` 前缀，无需用户修改已有清单。

### 代码数据结构调整示例

清单结构明确后，对应的 C++ 数据结构需同步调整。以下为 `src/core/manifest_config.h` 中的新增 / 变更结构：

```cpp
// In src/core/manifest_config.h

// Describes one pipeline node declared in the manifest.
// |name| must be unique within a single pipeline array.
struct ManifestPipelineNode {
    std::string name;                                       // Node name, e.g. "resize"
    std::unordered_map<std::string, std::string> params;    // Flat key-value params
};

// Manifest-layer tensor info with optional pipeline configuration.
// Separated from the public utils::TensorInfo to avoid leaking
// manifest-specific fields into the public header.
struct ManifestTensorInfo {
    std::string name;
    std::vector<int> shape;
    std::string dtype;
    std::string layout;
    bool has_normalize = false;
    NormalizeParams normalize;
    // Optional: explicit pipeline node chain. Empty = no preprocessing (identity passthrough).
    std::vector<ManifestPipelineNode> pipeline;
};

// ModelConfig changes: inputs/outputs use ManifestTensorInfo.
struct ModelConfig {
    std::string id;
    std::string name;
    std::string backend;
    std::string model_path;
    LoadStrategy load_strategy = LoadStrategy::kEager;
    std::vector<ManifestTensorInfo> inputs;   // changed from TensorInfo
    std::vector<ManifestTensorInfo> outputs;  // changed from TensorInfo
    std::unordered_map<std::string, std::string> config;
};
```

`ManifestParser` 新增 pipeline 字段解析与 name 唯一性校验：

```cpp
// In src/core/manifest_parser.cc — ParseTensorInfo():

constexpr const char* kKeyPipeline = "pipeline";
constexpr const char* kKeyName     = "name";
constexpr const char* kKeyParams   = "params";

if (j.contains(kKeyPipeline) && j[kKeyPipeline].is_array()) {
    std::unordered_set<std::string> seen_names;
    for (const auto& node_json : j[kKeyPipeline]) {
        ManifestPipelineNode node;
        if (!node_json.contains(kKeyName) ||
            !node_json[kKeyName].is_string()) {
            return utils::ErrorCode::kParseError;
        }
        node.name = node_json[kKeyName].get<std::string>();

        // Enforce name uniqueness within the same pipeline.
        if (!seen_names.insert(node.name).second) {
            return utils::ErrorCode::kParseError;  // Duplicate name
        }

        // Parse optional params as flat key-value.
        if (node_json.contains(kKeyParams) &&
            node_json[kKeyParams].is_object()) {
            for (const auto& [k, v] : node_json[kKeyParams].items()) {
                if (v.is_string()) {
                    node.params[k] = v.get<std::string>();
                } else if (v.is_number()) {
                    node.params[k] = std::to_string(v.get<double>());
                }
            }
        }
        info->pipeline.push_back(std::move(node));
    }
}
```

### 节点说明文档

在 `src/pipeline/nodes/` 下新增 `README.md`，作为内置节点的统一说明文档。公共模块 `src/public/` 同步导出该文档，供外部使用者参考。

文档结构示例：

```markdown
# Atlas Pipeline 内置节点参考

> 本文档由 src/pipeline/nodes/README.md 同步至公共模块。
> 外部使用者通过 Atlas 公共头文件集获取节点名称与参数规格。

## 节点清单

| name | 命名空间 | 说明 | 参数 |
|------|----------|------|------|
| `dtype_convert` | `atlas::` | 数据类型转换 | target: float32 / uint8 / int8 / ... |
| `resize` | `atlas::` | 双线性缩放 | height, width |
| `bgr_to_rgb` | `atlas::` | BGR → RGB 通道交换 | 无 |
| `rgb_to_bgr` | `atlas::` | RGB → BGR 通道交换 | 无 |
| `hwc_to_chw` | `atlas::` | HWC → CHW 布局转换 | 无 |
| `chw_to_hwc` | `atlas::` | CHW → HWC 布局转换 | 无 |
| `normalize` | `atlas::` | 逐通道归一化 | mean[], std[] |
| `softmax` | `atlas::` | Softmax 后处理 | axis (可选, 默认 -1) |
| `topk` | `atlas::` | Top-K 后处理 | k |

## 各节点详细说明

### dtype_convert
- 输入: 任意 dtype
- 输出: 目标 dtype
- 参数:
  - target (string, 必填): 目标数据类型
- 注册名: `atlas::dtype_convert`（manifest 中可简写为 `dtype_convert`）
- 示例: { "name": "dtype_convert", "params": { "target": "float32" } }

### resize
...
```

公共模块同步方式：在 `src/public/include/atlas/` 下不直接放置该文档（避免头文件目录混入 .md），而是在 `src/public/BUILD` 中通过 `data` 属性暴露 `src/pipeline/nodes/README.md`，或在 `tools/install_atlas.sh` 安装时一并复制到 `${PREFIX}/share/atlas/pipeline_nodes.md`。

### 分阶段 Profile 统计

管线执行是推理链路中不可忽略的耗时环节，需要纳入 profiling 分阶段统计。`ModelHandle::Run()` 中对以下三个阶段分别计时：

```
ModelHandle::Run()
  ├── [phase] input_pipeline    ← 管线预处理耗时
  ├── [phase] forward           ← IBackend::Infer() 推理耗时
  └── [phase] output_pipeline   ← 管线后处理耗时
```

统计记录复用 `ProfileRecord` 结构体（与 `ProfilingBackend` 共享）：

```cpp
struct ProfileRecord {
    std::string model_id;
    std::string phase;       // "infer"
    std::string step;        // "input_pipeline" / "forward" / "output_pipeline"
    double      duration_ms;
    int64_t     timestamp;
};
```

输出示例（CSV）：

```csv
model_id,phase,step,duration_ms,timestamp
face_detection,infer,input_pipeline,0.123,1720500001000
face_detection,infer,forward,12.345,1720500001012
face_detection,infer,output_pipeline,0.045,1720500001058
```

**设计要点：**

| 要点 | 说明 |
|------|------|
| **与 ProfilingBackend 互补** | `ProfilingBackend` 只统计 `IBackend::Infer()` 内部的子步骤；管线阶段的统计由 `ModelHandle::Run()` 自身完成 |
| **共享 ProfileRecorder** | 可抽出一个共享的 `ProfileRecorder` 工具类，`ProfilingBackend` 和 `ModelHandle::Run()` 共用同一 CSV 输出逻辑 |
| **按需启用** | 通过 Manifest 顶层 `"profile"` 节控制开关，与现有 profiling 配置一致 |

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 清单格式 | `pipeline` 数组为新增可选字段，不声明时行为不变（identity 直通） |
| 公共 API | `ModelHandle::Run` 签名不变，内部增加分阶段 profile 计时 |
| 删除字段 | **删除** `disable_pipeline` 字段，**删除** `Pipeline::BuildInputPipeline()` 自动构建方法 |
| 内部模块 | `ManifestParser`、`ManifestConfig`、`Pipeline`、`ModelManager`、`ModelHandle` |
| 新增模块 | `PipelineNodeFactory`（节点注册与创建，已实现） |
| 新增能力 | `ATLAS_REGISTER_PIPELINE_NODE` 宏开放给用户自定义节点注册 |
| **IPipelineNode 接口变更** | `Process()` 第 3 参数由 `const PipelineContext&` 改为 `const Context&`（`Context` 为 `IPipelineNode` 嵌套类型），携带 `config`/`backend`/`input_index`/`output_index`/`flags`/`args` 上下文 |
| **Context 结构体** | `config`（模型配置）、`backend`（IBackend*，用于零拷贝 buffer 访问）、`input_index`（当前 input 索引）、`output_index`（当前 output 索引）、`flags`（位掩码）、`args`（用户扩展数据指针） |
| **节点命名空间** | 内置节点使用 `atlas::` 前缀，用户节点使用自定义前缀（如 `mycompany::`），Factory 含简写兼容回退 |
| Profile | `ModelHandle::Run()` 新增三阶段计时：input_pipeline / forward / output_pipeline |
| 新增依赖 | 无 |
| 向后兼容 | Manifest 中无前缀节点名自动补全为 `atlas::` 前缀 |

### 更新日期

```diff
- 2026-07-10 初版修订
+ 2026-07-10 补充 PipelineContext + 命名空间设计
```

## 四、后续动作

- [ ] 评审讨论（记录日期与结论）
- [ ] 阶段归属：
  - `→ phase2.md`（Feature 记录章节已添加 Proposal-001 关联记录）
- [ ] 若采纳：按 `phase_spec.md` 流程推进实现
- [ ] 若驳回：填写驳回理由
