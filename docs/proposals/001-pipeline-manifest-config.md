# Proposal-001: Manifest 自由配置 Pipeline

> **提议日期**：2026-06-26
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：阶段级
> **关联**：`docs/phase2.md` 已知局限性、`docs/architecture.md` 第 3.5 节 Pipeline

## 一、背景

当前 Atlas 的 Pipeline 预处理链由 `Pipeline::BuildInputPipeline()` 根据 `TensorInfo` 自动构建，节点顺序固定为：

```
DtypeConvert → Resize → BGRToRGB → HWCToCHW → Normalize
```

该设计在阶段二实现时满足了基础需求，但在实际使用中存在以下局限性（详见 `docs/phase2.md` 已知局限性章节）：

1. **节点顺序固定**：用户无法在清单中调整节点顺序或跳过某个节点。例如某些模型需要 RGB→BGR 而非 BGR→RGB，或需要在 Resize 之后才做类型转换；
2. **仅支持 inputs[0]**：多输入模型只构建第一个输入的预处理管线，其余输入无预处理；
3. **无自定义节点清单配置**：清单未提供声明式描述「插入哪些节点」的字段，用户自定义节点只能通过 C++ 代码直接 `AddNode()` 添加，无法通过 manifest 配置；
4. **无输出后处理管线**：仅实现 `BuildInputPipeline`，未实现 `BuildOutputPipeline`，后处理（如 NMS、Softmax、TopK）需应用层自行处理。

`architecture.md` 第 3.5 节已规划「支持用户自定义管线节点插入」，本提案将其落地为具体方案。

## 二、方案概要

在 manifest 的 `inputs[]` 和 `outputs[]` 中新增可选的 `pipeline` 字段，允许用户以声明式方式定义预处理 / 后处理节点链。同时保留现有自动构建逻辑作为默认行为（向后兼容）。

> **唯一性约束**：同一 `pipeline` 数组内的节点 `name` 必须唯一，`ManifestParser` 在解析时检测重复并返回 `kParseError`。
>
> **禁用自动填充**：新增 `disable_pipeline`（布尔，默认 `false`）字段。设为 `true` 时，跳过 `BuildInputPipeline` 自动构建，用户必须通过 `pipeline` 数组显式声明节点链；若同时省略 `pipeline` 数组，则该输入无预处理（直通 identity）。参见下文「禁用自动填充」章节。

核心思路：

1. **清单扩展**：在 `TensorInfo` 中新增 `pipeline` 数组字段，每个元素描述一个节点（名称 + 参数），**同一 pipeline 内节点 `name` 必须唯一**；
2. **节点注册表**：引入 `PipelineNodeFactory`，将内置节点（DtypeConvert / Resize / BGRToRGB / HWCToCHW / Normalize）注册为可通过字符串名称创建；
3. **构建策略**：`BuildInputPipeline` 检测 `pipeline` 字段是否存在 — 若存在则按用户声明构建，否则走现有自动构建逻辑；
4. **多输入支持**：`ModelManager` 为每个 input 构建独立的 Pipeline，`ModelHandle::Run` 接受多输入；
5. **节点文档**：在 `src/pipeline/nodes/` 下新增节点说明文档，`src/public/` 公共模块同步导出该文档，供外部使用者参考。

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
        { "name": "dtype_convert", "params": { "target": "float32" } },
        { "name": "resize", "params": { "height": 224, "width": 224 } },
        { "name": "bgr_to_rgb" },
        { "name": "hwc_to_chw" },
        { "name": "normalize", "params": { "mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225] } }
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
        { "name": "softmax" },
        { "name": "topk", "params": { "k": 5 } }
      ]
    }
  ]
}
```

**省略 pipeline 字段（向后兼容，走自动构建）：**

```json
{
  "inputs": [
    {
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "normalize": { "mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225] }
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
};

// 内置节点注册
ATLAS_REGISTER_PIPELINE_NODE("resize", ResizeNode)
ATLAS_REGISTER_PIPELINE_NODE("normalize", NormalizeNode)
ATLAS_REGISTER_PIPELINE_NODE("softmax", SoftmaxNode)
// ...
```

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
    // Optional: explicit pipeline node chain. Empty = auto-build.
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

| name | 说明 | 参数 |
|------|------|------|
| dtype_convert | 数据类型转换 | target: float32 / uint8 / int8 / ... |
| resize | 双线性缩放 | height, width |
| bgr_to_rgb | BGR → RGB 通道交换 | 无 |
| rgb_to_bgr | RGB → BGR 通道交换 | 无 |
| hwc_to_chw | HWC → CHW 布局转换 | 无 |
| chw_to_hwc | CHW → HWC 布局转换 | 无 |
| normalize | 逐通道归一化 | mean[], std[] |
| softmax | Softmax 后处理 | axis (可选, 默认 -1) |
| topk | Top-K 后处理 | k |

## 各节点详细说明

### dtype_convert
- 输入: 任意 dtype
- 输出: 目标 dtype
- 参数:
  - target (string, 必填): 目标数据类型
- 示例: { "name": "dtype_convert", "params": { "target": "float32" } }

### resize
...
```

公共模块同步方式：在 `src/public/include/atlas/` 下不直接放置该文档（避免头文件目录混入 .md），而是在 `src/public/BUILD` 中通过 `data` 属性暴露 `src/pipeline/nodes/README.md`，或在 `tools/install_atlas.sh` 安装时一并复制到 `${PREFIX}/share/atlas/pipeline_nodes.md`。

### 禁用自动填充（disable_pipeline）

默认情况下，若未声明 `pipeline` 数组，`ModelManager::Init()` 会自动调用 `BuildInputPipeline` 构建默认预处理链。当输入数据已是模型可直接消费的格式（例如已做归一化的张量，或模型不需要预处理）时，自动构建会产生多余节点甚至错误。

新增 `disable_pipeline` 布尔字段（默认 `false`），语义如下：

| `disable_pipeline` | `pipeline` | 行为 |
|---|---|---|
| `false`（默认）| 不存在 | 自动构建（向后兼容） |
| `false`（默认）| `[...]` 非空 | 用户声明构建 |
| `true` | 不存在或 `[]` | 无 Pipeline（identity 直通） |
| `true` | `[...]` 非空 | 用户声明构建 |

判断优先级：**显式 pipeline 数组 > disable_pipeline > 自动构建**。

**示例 — 禁用自动构建，手动编排：**

```json
{
  "inputs": [
    {
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "disable_pipeline": true,
      "pipeline": [
        { "name": "hwc_to_chw" },
        { "name": "normalize", "params": { "mean": [0.5], "std": [0.5] } }
      ]
    }
  ]
}
```

**示例 — 完全禁用预处理（直通）：**

```json
{
  "inputs": [
    {
      "name": "features",
      "shape": [1, 512],
      "dtype": "float32",
      "disable_pipeline": true
    }
  ]
}
```

**数据结构变更（`ManifestTensorInfo`）：**

```cpp
struct ManifestTensorInfo {
    // ... existing fields ...
    bool disable_pipeline = false;  // NEW
    std::vector<ManifestPipelineNode> pipeline;
};
```

**构建逻辑变更（`ModelManager::Init`）：**

```cpp
for (const auto& input : model.inputs) {
    if (!input.pipeline.empty()) {
        entry.input_pipelines.push_back(
            pipeline::Pipeline::BuildFromManifest(input.pipeline));
    } else if (input.disable_pipeline) {
        entry.input_pipelines.push_back(pipeline::Pipeline{});  // identity
    } else {
        entry.input_pipelines.push_back(
            pipeline::Pipeline::BuildInputPipeline(input.ToTensorInfo()));
    }
}
```

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 清单格式 | 新增可选字段，向后兼容（不声明 pipeline 时行为不变） |
| 公共 API | `ModelHandle::Run` 签名可能扩展为支持多输入（需评估 ABI 影响） |
| 内部模块 | `ManifestParser`、`ManifestConfig`、`Pipeline`、`ModelManager`、`ModelHandle` |
| 新增模块 | `PipelineNodeFactory`（节点注册与创建） |
| 新增依赖 | 无 |
| 预估工作量 | 中等（清单解析扩展 + 工厂注册 + 多输入重构 + 测试） |

## 四、后续动作

- [ ] 评审讨论（记录日期与结论）
- [ ] 阶段归属：
  - `→ phase2.md`（Feature 记录章节已添加 Proposal-001 关联记录）
- [ ] 若采纳：按 `phase_spec.md` 流程推进实现
- [ ] 若驳回：填写驳回理由
