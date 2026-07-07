# Proposal-011: SNPE CPU 共享库对比测试模块

> **提议日期**: 2026-07-07
> **提议人**: Cline
> **状态**: 已采纳
> **类型**: 模块级
> **关联**: 无（新模块，非阶段功能变更）

## 一、背景

现有 `examples/shared_library/` 使用 Makefile + 共享库方式运行 CPU 后端（ONNX 模型），`examples/snpe_cpu/` 使用 Bazel 方式运行 SNPE 后端（DLC 模型）。存在以下不足：

1. **缺乏非 Bazel 项目对 SNPE 后端的集成验证**：SNPE 后端尚无 Makefile 集成测试，无法验证 SDK 发布后 SNPE 功能可用性；
2. **缺乏控制变量对比验证**：相同输入下 CPU（ONNX Runtime）与 SNPE（DLC）的推理结果一致性没有自动化验证手段；
3. **开发者测试流程割裂**：CPU/SNPE 各自有独立示例，缺少一个同时验证两种后端的统一入口。

因此需要创建一个新模块，以共享库的方式集成 SNPE 后端，并与 CPU 后端进行对比测试。

## 二、方案概要

在 `examples/snpe_cpu_shared/` 目录下，创建非 Bazel 的共享库测试模块：

### 2.1 文件布局

```
examples/snpe_cpu_shared/
├── Makefile              # 构建系统（参考 shared_library/Makefile）
├── main.cc               # 对比测试主程序
├── manifest.json         # 双模型清单（CPU + SNPE）
├── gen_models.py         # 模型生成脚本（参考 snpe_cpu/gen_models.py）
└── README.md             # 使用说明
```

### 2.2 manifest.json 设计

包含两个模型：
- `relu_cpu` — backend=`cpu`，加载 ONNX 格式的 ReLU 模型
- `relu_snpe` — backend=`snpe`，加载 DLC 格式的 ReLU 模型

两者 share 完全相同的 input/output shape（[1,3,4,4] NCHW float32），config 中 SNPE 指定 `"runtime": "cpu"`。

### 2.3 核心测试逻辑

1. 初始化 AtlasRuntime，加载同一个 manifest（同时含 CPU 和 SNPE 两个模型）
2. 生成一份合成输入张量（HWC uint8 渐变图像，同 `snpe_cpu/main.cc` 做法）
3. 分别通过 CPU handle 和 SNPE handle 执行推理
4. 逐元素比较两个输出，允许 1e-5 误差
5. 输出对比结论（一致 / 不一致 / 单侧失败）

### 2.4 测试结果输出格式

使用 CHECK 宏风格（同 `shared_library/main.cc`），不依赖内部 Logger 头文件。测试项包括：

| # | 测试项 | 预期 |
|---|--------|------|
| 1 | Runtime Init | kOk |
| 2 | GetModel("relu_cpu") | IsValid == true |
| 3 | GetModel("relu_snpe") | IsValid == true |
| 4 | CPU Run | kOk |
| 5 | SNPE Run | kOk（或 kBackendNotFound 对非 aarch64） |
| 6 | Output shape 一致 | 两者 shape 相同 |
| 7 | Output 逐元素比较 | 误差 < 1e-5 |
| 8 | 无效模型获取 | IsValid == false |
| 9 | Release → Re-init | 成功 |

### 2.5 与非 Bazel SDK 的集成

- 使用 `ATLAS_SDK` 环境变量定位 SDK，通过 Makefile 编译链接
- 使用 `SAMPLE_MODEL_DIR` 环境变量定位 DLC 模型文件（同 `snpe_cpu` 做法）
- ONNX 模型直接内置于示例目录中

## 三、影响范围

| 项目 | 说明 |
|------|------|
| 涉及模块 | 新增示例模块，不修改现有代码 |
| 公共 API | 无变更 |
| 新依赖 | 无（复用现有 ATLAS_SDK），模型生成需要 SNPE SDK（可选） |
| 预估工作量 | 新增约 200-300 行代码 + 文档 |
| 测试影响 | 无（不影响现有 8 个测试套件）|

## 四、后续动作

- [x] 评审讨论（2026-07-07，通过）
- [x] 已实现（小型模块级特性，直接编码实现）：
  - [x] 创建 `examples/snpe_cpu_shared/gen_models.py`
  - [x] 创建 `examples/snpe_cpu_shared/manifest.json`
  - [x] 创建 `examples/snpe_cpu_shared/main.cc`
  - [x] 创建 `examples/snpe_cpu_shared/Makefile`
  - [x] 创建 `examples/snpe_cpu_shared/README.md`
