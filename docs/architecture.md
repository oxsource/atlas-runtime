# Atlas 视觉模型运行框架 —— 架构总则

## 一、项目背景与目标

在视觉模型的部署实践中，面临以下核心问题：

- 一个清单文件统一管理多个模型；
- 不同硬件平台（高通、英伟达、瑞芯微等）的适配与扩展；
- 统一的输入输出接口，屏蔽底层差异；
- 可扩展的运行时后端实现。

Atlas 框架的目标是：**通过一个清单文件管理多个模型及其运行配置，程序初始化时解析清单并完成环境准备，上层应用通过统一接口简化对模型的调用。**

---

## 〇、AI 协作开发约定

本项目的文档与代码由 AI 辅助生成与实现。项目早期阶段由 GitHub Copilot / Claude 完成，后续阶段由 CodeBuddy / GLM 等工具接续，未来可能使用其他 AI 工具。**项目不限制必须使用何种 AI 模型或工具**，但遵循以下协作原则：

| 原则 | 说明 |
|------|------|
| **开发者主导** | 所有设计决策、阶段规划、方案评审由开发者（人类）做出，AI 不代替开发者做架构决策 |
| **AI 执行不越界** | AI 不应直接修改已有代码或文档而不经开发者确认；所有变更须由开发者引导发起 |
| **文档先行** | 任何代码实现前，须先有对应的设计文档（`proposals/` 或 `phase{N}.md`），AI 按文档实现，而非自行设计 |
| **变更可追溯** | AI 产出的每一处改动须能关联到对应的提议或阶段文档；开发者负责审查与合并 |

**典型协作流程：**

```
开发者：提出需求 / 评审方案 / 确认实现范围
    │
    ▼
AI：按 phase_spec.md / feature_spec.md 流程产出文档
    │
    ▼
开发者：评审文档，提出修改意见
    │
    ▼
AI：按确认的文档实现代码 + 测试
    │
    ▼
开发者：审查代码，确认合并
```

> **重要**：AI 在没有开发者明确指令的情况下，不应主动修改代码或定稿文档。若 AI 发现需要补充或修正的内容，应向开发者提出建议，由开发者决定是否执行。

---

## 二、整体架构

```
┌─────────────────────────────────────────────────────┐
│                   应用层 Application                  │
└───────────────────────┬─────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────┐
│               统一接口层 Atlas API                    │
│         Init / GetModel / Run / Release              │
└───────────┬───────────────────────┬─────────────────┘
            │                       │
┌───────────▼──────────┐ ┌──────────▼──────────────────┐
│    模型管理器          │ │    预处理/后处理管线 Pipeline  │
│    ModelManager       │ │  Resize / Normalize / ...   │
│  ┌────────────────┐   │ └─────────────────────────────┘
│  │ 清单解析器      │   │
│  │ ManifestParser │   │
│  └────────────────┘   │
│  ┌────────────────┐   │
│  │ 模型实例池      │   │
│  │ ModelPool      │   │
│  └────────────────┘   │
└───────────┬───────────┘
            │
┌───────────▼──────────────────────────────────────────┐
│               后端抽象层 Backend Abstraction           │
│        Load / Infer / GetInputInfo / Unload          │
└────┬──────────┬──────────┬──────────┬────────────────┘
     │          │          │          │
  ONNX RT   TensorRT    SNPE/QNN    RKNN
 (通用/CPU)  (NVIDIA)   (高通)    (瑞芯微)
```

---

## 三、核心模块说明

### 3.1 清单文件（Manifest）

采用 **YAML** 格式，统一描述所有模型的元信息与运行配置。

```
manifest.yaml
├── version            # 框架协议版本
├── name               # 应用/产品名称
└── models[]           # 模型列表
    ├── id             # 模型唯一标识
    ├── name           # 模型显示名称
    ├── backend        # 运行后端（onnx / tensorrt / snpe / rknn）
    ├── model_path     # 模型文件路径（支持环境变量）
    ├── inputs[]       # 输入描述
    │   ├── name       # 输入节点名
    │   ├── shape      # 形状（如 [1,3,640,640]）
    │   ├── dtype      # 数据类型（float32 / uint8 等）
    │   └── normalize  # 归一化参数（mean / std）
    ├── outputs[]      # 输出描述
    │   ├── name
    │   ├── shape
    │   └── dtype
    └── config{}       # 后端专属扩展配置（可选）
```

### 3.2 清单解析器（ManifestParser）

- 读取并校验 YAML 格式，版本兼容性检查；
- 支持环境变量替换（处理路径等动态配置）；
- 输出结构化的 `ManifestConfig` 对象供后续模块使用。

### 3.3 后端抽象层（Backend Abstraction）

定义统一的后端接口 `IBackend`：

| 接口 | 说明 |
|------|------|
| `Load(model_path, config)` | 加载模型文件及配置 |
| `Infer(inputs) → outputs` | 执行一次推理 |
| `GetInputInfo()` | 获取输入张量元信息 |
| `GetOutputInfo()` | 获取输出张量元信息 |
| `Unload()` | 卸载模型，释放资源 |

各平台后端独立实现此接口，通过 **后端工厂（BackendFactory）** 按名称注册与创建，结合编译期条件编译实现按平台裁剪。

### 3.4 模型管理器（ModelManager）

- 解析清单 → 按需通过 BackendFactory 实例化后端 → 维护模型实例池；
- 支持**懒加载**（首次使用时加载）与**预加载**两种策略；
- 提供线程安全的模型获取与释放。

### 3.5 预处理/后处理管线（Pipeline）

- 根据清单 inputs/outputs 中声明的 `pipeline` 数组显式构建，**无自动构建逻辑**；
- 内置常见预处理节点：Resize、Normalize、HWC→CHW、BGR→RGB、数据类型转换等；
- **开放** `ATLAS_REGISTER_PIPELINE_NODE` 宏，用户可注册任意自定义节点，manifest 中按 name 引用；
- **节点命名空间**：内置节点使用 `atlas::` 前缀（如 `atlas::resize`），用户节点使用自定义前缀（如 `mycompany::augment`），避免命名冲突；
- **`IPipelineNode::Context` 上下文**：`IPipelineNode::Process()` 接收 `Context`（作为首个参数，`IPipelineNode` 嵌套类型），携带 `config`（模型配置）、`backend`（`IBackend*`，用于零拷贝访问后端缓冲区）、`input_index` / `output_index`、`flags`（位掩码）、`args`（用户扩展数据指针）等链式调用常见上下文；
- 管线执行在 `ModelHandle::Run()` 内部，输出分阶段 Profile 统计（input_pipeline / forward / output_pipeline 三段耗时）。

### 3.6 统一接口层（Atlas API）

对上层应用暴露最简洁的 C++ 接口：

```
AtlasRuntime::Init(manifest_path)   // 初始化，解析清单
AtlasRuntime::GetModel(model_id)    // 获取模型句柄
ModelHandle::Run(input) → output    // 执行推理（含前后处理）
AtlasRuntime::Release()             // 释放所有资源
```

---

## 四、关键设计决策

| 决策点 | 选型 / 策略 | 理由 |
|--------|------------|------|
| 开发语言 | C++17 | 性能、跨平台、与主流推理库兼容 |
| 清单格式 | YAML | 可读性强，支持注释，yaml-cpp 成熟稳定 |
| 后端注册 | 工厂模式 + 编译期条件编译 | 按目标平台裁剪，不引入不需要的依赖 |
| Tensor 内存 | 统一 Tensor 类，零拷贝设计 | 避免跨模块数据冗余拷贝 |
| 错误处理 | 返回值 + ErrorCode，不抛异常 | 嵌入式 / 边缘设备友好 |
| 构建系统 | Bazel 6.5 | 多平台交叉编译，依赖隔离，可重现构建 |

---

## 五、目录结构规划

```
atlas/
├── docs/                   # 文档
│   ├── architecture.md     # 总则：整体架构、模块说明、阶段划分
│   ├── code_spec.md        # 代码规范（全局适用）
│   ├── doc_spec.md         # 文档管理总则：编写规范与定期梳理
│   ├── deps_spec.md        # 三方依赖规范：平台差异配置与 defs 复用
│   ├── feature_spec.md     # Feature 管理总则：提议收集与流转规范
│   ├── bugfix_spec.md      # Bugfix 管理总则：缺陷报告与修复规范
│   ├── phase_spec.md       # 阶段开发规范与流程
│   ├── proposals/          # 提议收集池（NNN-*.md）
│   ├── bugfixes/           # 缺陷报告池（BUG-NNN-*.md）
│   └── phase{N}.md         # 各阶段设计方案与结果汇总
├── manifest/               # 清单文件格式定义与示例
├── src/
│   ├── api/                # 对外暴露的统一 C++ API
│   ├── core/               # ManifestParser, ModelManager, ModelPool
│   ├── backend/            # 后端实现
│   │   ├── base/           # IBackend 接口定义 + BackendFactory
│   │   ├── onnx/           # ONNX Runtime 后端
│   │   ├── tensorrt/       # TensorRT 后端（NVIDIA）
│   │   ├── snpe/           # SNPE/QNN 后端（高通）
│   │   └── rknn/           # RKNN 后端（瑞芯微）
│   ├── pipeline/           # 预处理 / 后处理管线
│   └── utils/              # 日志（已实现）、错误码、类型定义、Tensor
├── tests/                  # 单元测试 + 集成测试
├── examples/               # 示例应用
├── third_party/            # 第三方依赖（yaml-cpp 等）
└── BUILD / WORKSPACE       # Bazel 构建入口
```

---

## 六、开发阶段划分

| 阶段 | 内容 | 产出 |
|------|------|------|
| 阶段一 | Bazel 环境搭建 + 清单解析器 + 后端接口定义 | 可解析清单，框架骨架完整 |
| 阶段二 | CPU 后端（ONNX Runtime）+ 基础 Pipeline | 第一个可运行的端到端推理链路 |
| 阶段三 | ModelManager + Atlas API + 单元测试 | 完整 API 可用，测试覆盖核心路径 |
| 阶段四 | TensorRT / RKNN / SNPE 后端按需接入 | 多平台支持 |
| 阶段五 | 示例应用 + 性能基准测试 + 发布 | 可对外使用的正式版本 |
| 阶段六 | 对外提供库及接口（共享库 + Bazel 依赖集成 + 公共头文件 + 打包安装） | 可被外部项目集成使用的共享库与稳定公共 API |

> **重要**：开发阶段划分为固定框架，不再新增阶段。后续所有 feature 提议经评审采纳后，只能关联补充到已有的阶段文档中（按 `feature_spec.md` 4.5 节补充标注规范执行），不得创建新的 `phase{N}.md`。
