# SNPE 量化模型开发指南 — ITensor 与 UserBuffer 双模式

> **文档版本**: 1.0 / **对应代码**: phase5+ / **最后更新**: 2026-07-14 / **状态**: 已发布
>
> **关联文档**:
> - [Proposal-015: SNPE UserBuffer 机制适配](../proposals/015-snpe-user-buffer-adaptation.md)
> - [Proposal-002: SNPE Backend](../proposals/002-snpe-backend.md)
> - [Proposal-012: SNPE Backend 推理优化](../proposals/012-snpe-backend-inference-optimization.md)
> - [`src/backend/snpe/snpe_backend_v1.cc`](../../src/backend/snpe/snpe_backend_v1.cc)
> - [`src/backend/snpe/snpe_backend_v2.cc`](../../src/backend/snpe/snpe_backend_v2.cc)
> - [`src/api/model_handle.h`](../../src/api/model_handle.h)
>
> **SNPE SDK 参考示例**（用于交叉验证实现正确性）：
> - **v1 (1.50.0.2622)**: `/opt/qcom/sdk/snpe-1.50.0.2622/examples/NativeCpp/SampleCode/jni/`
> - **v2 (2.21.0.240401)**: `/opt/qcom/aistack/qairt/2.21.0.240401/examples/SNPE/NativeCpp/SampleCode/jni/`

---

## 目录

1. [概述](#1-概述)
2. [量化模型基础概念](#2-量化模型基础概念)
3. [ITensor 模式（默认）](#3-itensor-模式默认)
4. [UserBuffer 模式](#4-userbuffer-模式)
5. [Manifest 配置详解](#5-manifest-配置详解)
6. [API 调用模式](#6-api-调用模式)
7. [量化适配：TF8 模型与 U8 输入](#7-量化适配-tf8-模型与-u8-输入)
8. [零拷贝输入的三级方案](#8-零拷贝输入的三级方案)
9. [多模型共享输入缓冲](#9-多模型共享输入缓冲)
10. [完整示例](#10-完整示例)
11. [常见问题与排查](#11-常见问题与排查)

附录：
- [A: SNPE SDK 版本关键流程对比](#附录-ssnpe-sdk-版本关键流程对比)
- [B: SnpeMemoryPool 工作原理](#附录-bsnpememorypool-工作原理)
- [C: ITensor 与 UserBuffer 内部数据流对比](#附录-citensor-与-userbuffer-内部数据流对比)
- [D: QuantParams 与 UserBufferEncoding 映射](#附录-dquantparams-与-userbufferencoding-映射)

---

## 1. 概述

SNPE（Snapdragon Neural Processing Engine）是 Qualcomm 提供的深度学习推理引擎。atlas-runtime 的 SNPE 后端支持 **两种推理模式**：

| 模式 | builder 配置 | Infer 路径 | 适用场景 |
|------|-------------|-----------|---------|
| **ITensor** | `use_buffer=false` (默认) | `snpe->execute(ITensor, TensorMap)` | 简单推理、CPU/GPU 运行时 |
| **UserBuffer** | `use_buffer=true` | `snpe->execute(UserBufferMap, UserBufferMap)` | DSP/HTP 运行时、零拷贝输入、多模型共享缓冲 |

**核心区别**：ITensor 模式下 SNPE 内部管理内存布局，用户通过 float* 接口读写；UserBuffer 模式下用户直接管理对齐内存，SNPE 从用户指定的缓冲区直接读取，实现真正的零拷贝。

---

## 2. 量化模型基础概念

### 2.1 SNPE 量化类型

SNPE 量化模型使用 **TF8（int8）** 或 **UINT8** 表示量化张量：

| SNPE ElementType | atlas DataType | 取值范围 | 说明 |
|------------------|---------------|----------|------|
| `FLOAT` | `kFloat32` | IEEE 754 | 浮点模型 |
| `TF8` | `kInt8` | [-128, 127] | 量化到 int8，带 scale/zero_point |
| `UNSIGNED8BIT` | `kUInt8` | [0, 255] | 无符号 8 位（少见） |
| `TF16` | `kFloat16` | 半精度 | 量化到 float16 |

### 2.2 量化参数

TF8 量化的核心参数：

```
real_value = (quantized_value - zero_point) × scale
```

- **scale**（`quantized_step_size`）：每个量化步长代表的实数值
- **zero_point**（`step_exactly_0`）：实数 0 对应的量化值
- **bandwidth**：位宽，TF8 = 8

这些参数由 SNPE 在模型编译时从 `.dlc` 中提取，存储在 `IBufferAttributes` 中。atlas-runtime 在 `BuildTensorInfos()` 阶段自动采集并存储为 `SnpeBackend::QuantParams`：

```cpp
struct QuantParams {
    float    scale      = 1.0f;
    int32_t  zero_point = 0;
    uint32_t bandwidth  = 8;
};
```

> **开发者无需手动处理量化参数**。UserBuffer 模式下，后端自动使用这些参数创建 `UserBufferEncodingTfN`，SNPE 内部自动完成量化和反量化。

---

## 3. ITensor 模式（默认）

### 3.1 工作原理

ITensor 是 SNPE 的默认推理路径：

```
┌──────────────┐     memcpy     ┌──────────────────┐
│  用户输入数据  │ ──────────→   │  ITensor (float)  │
└──────────────┘               └────────┬─────────┘
                                        │
                              ┌─────────▼─────────┐
                              │  snpe->execute()   │
                              │  (内部反量化→推理)  │
                              └─────────┬─────────┘
                                        │
                              ┌─────────▼─────────┐
                              │  TensorMap (输出)  │
                              │  (零拷贝引用)      │
                              └───────────────────┘
```

### 3.2 特点

| 方面 | 说明 |
|------|------|
| 输入接口 | `float*` 指针（ITensor 内部始终为 float） |
| 输入拷贝 | 每次 Infer() 至少一次 memcpy |
| 输出获取 | 零拷贝——`TensorMap` 返回内部指针 |
| 内存对齐 | 无特殊要求 |
| 量化处理 | SNPE 内部自动进行输入反量化和输出量化 |

### 3.3 适用场景

- CPU/GPU 运行时
- 单模型、低吞吐场景
- 不需要外部内存注入
- 量化感知处理由 SNPE 内部完成即可

---

## 4. UserBuffer 模式

### 4.1 工作原理

UserBuffer 模式下，用户分配对齐内存，SNPE 直接读写该内存：

```
┌──────────────┐    直接写入     ┌──────────────────┐
│  用户输入数据  │ ──────────→   │  AlignedBuffer    │  (128B 对齐)
└──────────────┘               │  (UserBuffer 后备) │
                               └────────┬─────────┘
                                        │
                              ┌─────────▼─────────┐
                              │  snpe->execute()   │
                              │  (直接读写用户内存) │
                              └─────────┬─────────┘
                                        │
                               ┌────────▼────────┐
                               │  AlignedBuffer   │  (输出结果直接在此)
                               │  (零拷贝到用户)  │
                               └─────────────────┘
```

### 4.2 与 ITensor 的对比

| 对比项 | ITensor | UserBuffer |
|--------|---------|------------|
| 输入拷贝次数 | 1 次（`memcpy` 到 ITensor） | 0 次（直接写入后备缓冲） |
| 输入数据类型 | 仅 float | 与模型 dtype 一致（TF8/U8/F32） |
| 内存对齐 | 无要求 | **128 字节对齐**（DSP 要求） |
| 量化参数传递 | SNPE 内部处理 | 通过 `UserBufferEncodingTfN` 显式传递 |
| `SetInputBuffer()` 支持 | ❌ | ✅ |
| 多模型共享输入 | ❌ | ✅ |
| DSP/HTP 最佳实践 | ❌ | ✅ |

### 4.3 UserBuffer 支持的数据类型

UserBuffer 模式支持的数据类型由 `CreateEncoding()` 函数决定（`src/backend/snpe/snpe_backend_v1.cc`）。

根据 SNPE SDK v1.50.0.2622 `NativeCpp/SampleCode` 官方示例：

- `main.cpp` 接受 `-b USERBUFFER_FLOAT` / `USERBUFFER_TF8` / `USERBUFFER_TF16` / `ITENSOR`
- `CreateUserBuffer.cpp` 根据 `isTfNBuffer` 标志创建不同的 encoding：

```cpp
// SNPE SDK v1.50.0 NativeCpp — CreateUserBuffer.cpp
if (isTfNBuffer) {
    // TF8 / TF16 都通过 UserBufferEncodingTfN 实现，bitWidth 区分
    userBufferEncoding = std::make_unique<zdl::DlSystem::UserBufferEncodingTfN>(
        0, 1.0f, bitWidth);  // bitWidth = 8 (TF8) 或 16 (TF16)
} else {
    userBufferEncoding = std::make_unique<zdl::DlSystem::UserBufferEncodingFloat>();
}
```

atlas-runtime 的 v1 和 v2 各自使用独立的 `CreateEncoding()` 实现，与 SDK 版本对齐：

**v1 (snpe_backend_v1.cc)** — 与 SDK v1.50.0 一致，仅 Float/TfN 两种编码：
```cpp
case kFloat32 → UserBufferEncodingFloat
case kInt8    → UserBufferEncodingTfN(zp, scale, bandwidth)  // TF8
case kUInt8   → UserBufferEncodingTfN(0, 1.0f, 8)           // TfN 模拟
default       → nullptr                                      // 不支持
```

**v2 (snpe_backend_v2.cc)** — 与 SDK v2.21.0 一致，独立编码类型：
```cpp
case kFloat32 → UserBufferEncodingFloat
case kFloat16 → UserBufferEncodingFloat16
case kInt8    → UserBufferEncodingTfN(zp, scale, bandwidth)
case kUInt8   → UserBufferEncodingUint8
case kInt32   → UserBufferEncodingInt32
default       → nullptr
```

v1 与 v2 的底层 `ParseDataType()` 映射也有差异：

| SNPE ElementType | v1 映射 | v2 映射 |
|-----------------|---------|---------|
| `FLOAT` | `kFloat32` ✅ | `kFloat32` ✅ |
| `TF8` | `kInt8` ✅ | `kInt8` (v2 用 `INT8`) ✅ |
| `TF16` | `kUInt8` ❌ | `kFloat16` ✅ |
| `UNSIGNED8BIT` | `kUInt8` ✅ | `kUInt8` ✅ |
| `FLOAT16` | ❌ 不存在 | `kFloat16` ✅ |
| `INT32` | ❌ 不存在 | `kInt32` ✅ |

> **v1 与 v2 的 TF16 实现差异**：v1 没有 `UserBufferEncodingFloat16` 独立编码类，因此 TF16 通过
> `UserBufferEncodingTfN(zp, scale, 16)` 实现（`bitWidth=16`，与 SDK v1.50.0 官方示例一致）。
> v2 则使用独立的 `UserBufferEncodingFloat16` 编码。

支持的 DataType 汇总：

| DataType | v1 (1.50.0) | v2 (2.21.0) |
|----------|-------------|-------------|
| `kFloat32` | ✅ `UserBufferEncodingFloat` | ✅ `UserBufferEncodingFloat` |
| `kInt8` (TF8) | ✅ `UserBufferEncodingTfN(zp, scale, 8)` | ✅ `UserBufferEncodingTfN(zp, scale, 8)` |
| `kUInt8` | ✅ `TfN(0, 1.0, 8)` 模拟 | ✅ `UserBufferEncodingUint8` |
| `kFloat16` | ✅ `UserBufferEncodingTfN(zp, scale, 16)` | ✅ `UserBufferEncodingFloat16` |
| `kInt32` | ❌ | ✅ `UserBufferEncodingInt32` |

> 相比 ITensor 路径（仅支持 float 输入），UserBuffer 可直接传入量化后的 dtype（`kInt8`/`kUInt8`），**省去一次内部反量化的隐式转换**。
> 不支持的 dtype 会被 `CreateEncoding` 返回 nullptr，`Load()` 报 `kInferFailed`。

### 4.4 何时使用 UserBuffer

1. **DSP/HTP 运行时**：Qualcomm 官方推荐 DSP 运行时使用 UserBuffer
2. **零拷贝输入**：从摄像头、解码器直接传递缓冲区
3. **多模型共享输入**：多个模型使用同一份输入数据
4. **减少内存占用**：共享缓冲模式可将 N 份输入内存降到 1 份

---

## 5. Manifest 配置详解

### 5.1 基础配置

```json
{
  "version": "1.0",
  "name": "snpe-quantized-model",
  "models": [
    {
      "id": "my_model",
      "backend": "snpe",
      "model_path": "${MODEL_DIR}/quantized.dlc",
      "config": {
        "runtime": "cpu",
        "use_buffer": "false",
        "performance_profile": "high_performance"
      }
    }
  ]
}
```

### 5.2 配置项完整列表

| 配置键 | 可选值 | 默认值 | 说明 |
|--------|-------|--------|------|
| `runtime` | `cpu` / `gpu` / `dsp` | `gpu` | 目标运行时 |
| `use_buffer` | `true` / `false` | `false` | 启用 UserBuffer 模式 |
| `performance_profile` | 见下方 | `high_performance` | 性能配置文件 |
| `shared_input` | 任意字符串 | 空（不共享） | 共享输入缓冲 key |
| `cpu_fixed_point` | `true` / `false` | `false` | v2 仅；CPU runtime 上以定点模式执行量化 DLC |

`performance_profile` 可选值：

| 值 | SNPE 枚举 |
|----|-----------|
| `default` / `balanced` | `BALANCED` |
| `high_performance` | `HIGH_PERFORMANCE` |
| `power_saver` | `POWER_SAVER` |
| `system_settings` | `SYSTEM_SETTINGS` |
| `sustained_high_performance` | `SUSTAINED_HIGH_PERFORMANCE` |
| `burst` | `BURST` |
| `low_power_saver` | `LOW_POWER_SAVER` |
| `high_power_saver` | `HIGH_POWER_SAVER` |
| `low_balanced` | `LOW_BALANCED` |

### 5.3 量化模型感知配置

当模型是量化模型（TF8/U8）时，manifest 中可以通过 `inputs[].dtype` 声明外部输入的实际数据类型。这在 **TF8 模型接收 U8 输入**的场景下至关重要：

```json
{
  "models": [
    {
      "id": "detection",
      "backend": "snpe",
      "model_path": "${MODEL_DIR}/yolo.dlc",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true"
      },
      "inputs": [
        {
          "name": "data",
          "shape": [1, 3, 640, 640],
          "dtype": "uint8",
          "layout": "NCHW"
        }
      ]
    }
  ]
}
```

**关键规则**：

| Manifest 设置 | 效果 |
|--------------|------|
| 不设置 `inputs[].dtype` | 使用 SNPE runtime 报告的 dtype（TF8 模型报告 `kInt8`） |
| `inputs[].dtype = "uint8"` | 触发 U8→TF8 适配转换，且 UserBuffer 的 encoding 按 U8 创建 |
| `inputs[].dtype = "float32"` | UserBuffer 创建 `UserBufferEncodingFloat`，SNPE 内部处理反量化 |

> **manifest 中的 dtype 是"外部输入的实际类型"**，而非"想让模型变成什么类型"。后端会自动处理外部类型到模型内部类型的适配。

### 5.4 输出格式转换（Output dtype override）

`outputs[].dtype` 的作用与 `inputs[].dtype` **完全不同**。它不是"声明外部输出的实际类型"，而是**请求 SNPE 以指定格式写入输出 UserBuffer**。这是 UserBuffer 模式提供的通用能力——通过 output encoding 指示 SNPE 自动完成数据格式转换。

```json
{
  "models": [
    {
      "id": "detection",
      "backend": "snpe",
      "model_path": "${MODEL_DIR}/yolo_tf8.dlc",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true"
      },
      "inputs": [
        {
          "name": "data",
          "dtype": "uint8"
        }
      ],
      "outputs": [
        {
          "name": "output",
          "dtype": "float32"
        }
      ]
    }
  ]
}
```

上例中模型内部推理结果为 TF8（int8），但由于 output UserBuffer 的 encoding 设为 `UserBufferEncodingFloat`，SNPE 在写入输出文件缓冲区时自动完成 TF8→float32 反量化。**这与 ITensor 模式下 SNPE 内部自动反量化的行为一致**，只是 UserBuffer 模式下必须通过 manifest 显式声明。

作用机制：

```
内部推理结果 (TF8)
      │
      ▼  SNPE 根据输出 UserBufferEncoding 自动转换
      │
输出 UserBuffer (格式由 encoding 决定)
      │
      ├── UserBufferEncodingFloat  → 输出 float32 (自动反量化)
      ├── UserBufferEncodingTfN    → 输出 TF8 int8  (保持量化格式)
      └── UserBufferEncodingUint8  → 输出 uint8     (SNPE 2.x)
```

对比 input/output 的 dtype 语义：

| 设置 | 语义 | 机制 |
|------|------|------|
| `inputs[].dtype = "uint8"` | "我的外部输入是 U8" | 触发 `AdaptU8ToTf8()` 适配转换 |
| `outputs[].dtype = "float32"` | "请以 float32 格式输出" | 通过 `UserBufferEncodingFloat` 让 SNPE 内部反量化 |

> 不设置 `outputs[].dtype` 时，输出保持模型原始 dtype（TF8 模型输出即为 int8）。此时如果需要格式转换，可通过 pipeline 的 `dtype_convert` 节点在后处理中完成。

### 5.5 命名规范

Manifest 中 tensor 的 `name` 必须与 `.dlc` 模型中的 tensor 名称**完全匹配**。可以通过以下方式查看模型 tensor 名称：

```bash
# SNPE 提供的工具
snpe-dlc-info -i model.dlc

# 或者使用 snpe-dlc-viewer
snpe-dlc-viewer -i model.dlc
```

---

## 6. API 调用模式

### 6.1 标准调用（Level 1 — 自动拷贝）

最简单的方式，适合大多数场景。运行时自动根据 manifest 中的 `use_buffer` 选择推理路径。

```cpp
#include "atlas/atlas.h"

// 1. 初始化运行时
atlas::AtlasRuntime runtime;
auto ret = runtime.Init("manifest.json");
assert(ret == atlas::utils::ErrorCode::kOk);

// 2. 获取模型句柄
auto model = runtime.GetModel("my_model");

// 3. 准备输入数据
auto input_info = model.GetInputInfoAt(0);
atlas::utils::Tensor input_tensor;
input_tensor.info = input_info;
input_tensor.EnsureCapacity(atlas::utils::ElementCount(input_info.shape) *
                            atlas::utils::ElementByteSize(input_info.dtype));
// 填充数据...
std::memcpy(input_tensor.data, image_data, input_tensor.byte_size);

// 4. 推理
std::vector<atlas::utils::Tensor> outputs;
ret = model.Run(input_tensor, &outputs);
assert(ret == atlas::utils::ErrorCode::kOk);

// 5. 处理输出
for (const auto& out : outputs) {
    // out.data 指向推理结果
    ProcessOutput(out);
}
```

### 6.2 零拷贝输入（Level 2 — 写入内部缓冲）

通过 `GetInputTensor()` 获取后端内部缓冲区的 Tensor，直接写入数据后传入 `Run()`。

```cpp
// 获取内部输入缓冲 Tensor（与 GetInputBuffer() 共享同一内存）
auto input_tensor = model.GetInputTensor(0);
if (input_tensor.data == nullptr) {
    // 后端不支持零拷贝输入
    return;
}

// 直接写入内部缓冲 — 零拷贝
std::memcpy(input_tensor.data, image_data, input_tensor.byte_size);

// 调用 Run() 时传入同一 Tensor（后端检测到 data 指向内部缓冲，跳过 memcpy）
std::vector<atlas::utils::Tensor> outputs;
ret = model.Run(input_tensor, &outputs);
```

**内部检测逻辑**：
```
if (t.data == GetInputBuffer(i).data) {
    // 数据已直接写入内部缓冲 — 跳过拷贝
    goto execute;
}
```

### 6.3 外部内存注入（Level 3 — SetInputBuffer）

仅 UserBuffer 模式支持。`SetInputBuffer()` 注册一个外部数据源，推理时后端从该外部源**拷贝**到内部预分配的对齐缓冲区中（`user_input_raw`），再由 SNPE 读取执行。**这不是零拷贝**——内部仍有 1 次 memcpy。

```cpp
// 仅 use_buffer=true 时有效
ret = model.SetInputBuffer(0, camera_frame, frame_size);
assert(ret == atlas::utils::ErrorCode::kOk);

// 推理时从 camera_frame memcpy 到内部 UserBuffer 再执行
std::vector<atlas::utils::Tensor> outputs;
ret = model.Run(input_tensor, &outputs);

// 重置为内部缓冲
model.SetInputBuffer(0, nullptr, 0);
```

**使用限制**：
- 仅 `use_buffer=true` 时支持
- 外部内存必须保持有效直到 `Run()` 返回
- 外部内存大小必须 ≥ 内部 UserBuffer 大小
- 调用 `SetInputBuffer()` 后，`Run()` 的 input tensor 数据被**忽略**（从外部内存拷贝到内部缓冲再执行）

> **与 Level 2 的对比**：`SetInputBuffer` 仅提供"调用方无需手动传入数据"的便利，内部仍有 1 次 memcpy。
> 如果需要**真正的零拷贝**（0 次额外拷贝），请使用 [Level 2 方式](#62-零拷贝输入level-2--写入内部缓冲)：
> 通过 `GetInputTensor()` 获取内部预分配缓冲的 Tensor，直接写入数据后传入 `Run()`。
> 后端检测到 `t.data` 与内部缓冲是同一指针，跳过 memcpy。

### 6.4 UserBuffer 适配量化（TF8 模型 + U8 输入）

当模型量化为 TF8（int8）而外部输入为 U8（uint8）时，manifest 中声明 `dtype: "uint8"`，后端自动处理适配：

```cpp
// manifest 配置中 inputs[0].dtype = "uint8"
// 模型实际 dtype = kInt8 (TF8)

// 用户按 U8 传入
atlas::utils::Tensor input_tensor;
input_tensor.info.dtype = atlas::utils::DataType::kUInt8;
input_tensor.data = u8_image_data;  // uint8_t [0, 255]

// 后端 InferWithBuffer() 自动检测到需要适配
// NeedsQuantizationAdaptation() = true
// 执行 AdaptU8ToTf8(): int8_val = uint8_val - 128
model.Run(input_tensor, &outputs);
```

### 6.5 多模型共享输入缓冲

多模型共享输入缓冲有两种方式实现，但只有 **Level 2 方式是真正的零拷贝**：

**方式一（推荐）— Level 2 零拷贝共享**：
```cpp
// manifest 中两个模型配置了相同的 shared_input key
// 在 Load() 中两个模型的 user_input_raw 已指向同一块 pool 内存

auto input_a = model_a.GetInputTensor(0);  // 返回 user_input_raw 指针
auto input_b = model_b.GetInputTensor(0);  // 同一指针
assert(input_a.data == input_b.data);

// 写入一次，两个模型共享
std::memcpy(input_a.data, image_data, input_a.byte_size);

// 串行推理（零拷贝）
model_a.Run(input_a, &outputs_a);
model_b.Run(input_b, &outputs_b);
```

**方式二 — Level 3 注册同一外部源（非零拷贝）**：
```cpp
model_a.SetInputBuffer(0, camera_frame, frame_size);
model_b.SetInputBuffer(0, camera_frame, frame_size);

// 推理时各自从 camera_frame memcpy 到内部缓冲，仍有 1 次拷贝
model_a.Run(input_tensor, &outputs_a);
model_b.Run(input_tensor, &outputs_b);
```

---

## 7. 量化适配：TF8 模型与 U8 输入

### 7.1 背景

许多量化工具链（如 SNPE 的 `snpe-dlc-quantize`）将模型量化到 **TF8（int8，范围 -128~127）**，但常见的输入源（摄像头、解码器、图片文件）输出的是 **U8（uint8，范围 0~255）**。

如果不做适配，将 U8 数据直接当作 TF8 传入会导致：
- 输入范围错误（0~255 被解释为 -128~127）
- 推理结果完全错误

### 7.2 适配场景矩阵

| 模型内部 dtype | 外部输入 dtype | 是否需要适配 | 适配方式 |
|---------------|---------------|-------------|---------|
| `kInt8` (TF8) | `kInt8` (TF8) | ❌ | 直接传入 |
| `kInt8` (TF8) | `kUInt8` (U8) | ✅ | `output = input - 128` |
| `kUInt8` | `kUInt8` (U8) | ❌ | 直接传入 |
| `kFloat32` | `kFloat32` | ❌ | 直接传入 |

### 7.3 配置方式

**方式一：manifest 中声明（推荐）**

```json
"inputs": [
  {
    "name": "data",
    "dtype": "uint8"
  }
]
```

后端在 `BuildTensorInfos()` 中检测到 manifest 覆盖了 dtype，自动：
1. 将 `input_info_[i].dtype` 设为 `kUInt8`
2. 重置量化参数为 U8 默认值
3. 在 `InferWithBuffer()` 中检测到模型实际 dtype 与输入 dtype 不匹配时，自动执行 `AdaptU8ToTf8()`

**方式二：不声明，传入 U8 Tensor**

不声明 dtype 时，`input_info_[i].dtype` 保持 SNPE runtime 报告的值（TF8 模型报告 `kInt8`）。如果用户传入的 `t.info.dtype` 为 `kUInt8`，后端同样会触发适配：

```cpp
// Manifest 中未声明 dtype
atlas::utils::Tensor input_tensor;
input_tensor.info.dtype = atlas::utils::DataType::kUInt8;  // 显式声明
input_tensor.data = u8_data;
model.Run(input_tensor, &outputs);  // 触发适配
```

### 7.4 适配实现细节

```cpp
// 核心转换逻辑 (snpe_backend_v1.cc)
void AdaptU8ToTf8(const void* src, void* dst, size_t count) {
    const auto* u8_src = static_cast<const uint8_t*>(src);
    auto* s8_dst = static_cast<int8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        s8_dst[i] = static_cast<int8_t>(static_cast<int>(u8_src[i]) - 128);
    }
}

// 触发条件
bool NeedsQuantizationAdaptation(
    const std::vector<utils::TensorInfo>& info,
    size_t index, utils::DataType input_dtype) {
    return (info[index].dtype == utils::DataType::kInt8 &&   // 模型实际 TF8
            input_dtype == utils::DataType::kUInt8);         // 输入为 U8
}
```

### 7.5 输出侧注意事项

**后端不做输出侧自动反转换。** 推理结果始终保持模型原始格式（TF8）。原因：
- 输出可能用于 softmax/argmax 等数值敏感处理，自动转换丢失精度
- 数据转换应由 pipeline 中的显式 Node 完成

如果需要将 int8 输出转换为 uint8，在 pipeline 中插入 `dtype_convert` 节点：

```json
"outputs": [
  {
    "name": "output",
    "pipeline": [
      {"name": "dtype_convert", "params": {"target": "float32"}}
    ]
  }
]
```

---

## 8. 零拷贝输入的三级方案

### 8.1 三级总览

| 级别 | 名称 | 额外拷贝次数 | API | 支持模式 |
|------|------|------------|-----|---------|
| Level 1 | 标准拷贝 | 1 次 memcpy | `Run(input, &outputs)` | ITensor / UserBuffer |
| Level 2 | 内部缓冲写入 | **0 次** | `GetInputTensor()` + `Run()` | ITensor / UserBuffer |
| Level 3 | 外部内存注入 | **1 次**（固定） | `SetInputBuffer()` + `Run()` | **仅 UserBuffer** |

### 8.2 Level 1：标准拷贝（默认）

```cpp
// 最简单的使用方式 — 每次 Run() 内部执行一次 memcpy
atlas::utils::Tensor input;
input.data = raw_data;
input.byte_size = data_size;
model.Run(input, &outputs);
```

内部流程：
```
用户输入 → memcpy → 内部缓冲 (ITensor / AlignedBuffer) → SNPE execute
```

### 8.3 Level 2：内部缓冲写入

```cpp
// 获取后端内部缓冲 — 写入不额外拷贝
auto input_tensor = model.GetInputTensor(0);
memcpy(input_tensor.data, raw_data, raw_size);
// Run() 检测到 input_tensor.data 指向内部缓冲，跳过 memcpy
model.Run(input_tensor, &outputs);
```

内部流程：
```
用户 memcpy → 内部缓冲 (ITensor / AlignedBuffer) → SNPE execute (跳过二次拷贝)
```

**最佳实践**：如果 pipeline 中有预处理节点且数据流向为 `input → pipeline → backend`，Level 2 的零拷贝通过 pipeline 的输出直接指向 backend 内部缓冲实现。

### 8.4 Level 3：外部内存注入

```cpp
// 仅 UserBuffer 模式
model.SetInputBuffer(0, dma_buffer, buffer_size);
// Run() 从 dma_buffer 拷贝到内部 UserBuffer 再执行（仍有 1 次 memcpy）
model.Run(input_tensor, &outputs);

// 多模型共享同一外部内存
model_a.SetInputBuffer(0, shared_buffer, buffer_size);
model_b.SetInputBuffer(0, shared_buffer, buffer_size);
```

内部流程：
```
SetInputBuffer 注册 → Infer 时 memcpy(外部内存 → 内部 UserBuffer) → SNPE execute → 仍有 1 次拷贝
```

### 8.5 三级选择指南

| 场景 | 推荐级别 | 理由 |
|------|---------|------|
| 简单集成，单模型 | Level 1 | 最少代码，性能够用 |
| 已自行分配对齐内存 | Level 2 | **真正的零拷贝**（0 次额外拷贝） |
| 摄像头/DMA 缓冲区 | Level 2 | 通过 `GetInputTensor()` 写入内部预分配缓冲，零拷贝 |
| 多模型共享输入 | Level 2 | 共享的 `user_input_raw` 写入一次，多个模型复用 |
| DSP/HTP 运行时 | Level 2 | DSP 官方推荐 UserBuffer + `GetInputTensor` 零拷贝 |
| 需要注册外部数据源 | Level 3 | 调用方无需手动传入数据，但无法避免 1 次拷贝 |

---

## 9. 多模型共享输入缓冲

### 9.1 原理

通过 `SnpeMemoryPool` 的 `AcquireShared(key)` / `ReleaseShared(key)` 接口，多个模型共享同一块物理内存，消除重复分配和拷贝。

```
┌─────────────────────────────────┐
│       SnpeMemoryPool            │
│  ┌─────────────────────────────┐│
│  │  shared_buffers_:           ││
│  │  "camera_feed" → [mem, rc=2]││
│  └─────────────────────────────┘│
└──────────┬──────────────────────┘
           │ 同一个 data 指针
     ┌─────┴─────┐
     ▼           ▼
  model_a      model_b
  UserBuffer   UserBuffer
```

### 9.2 配置方式

```json
[
  {
    "id": "detector",
    "backend": "snpe",
    "config": {
      "use_buffer": "true",
      "shared_input": "camera_feed"
    }
  },
  {
    "id": "classifier",
    "backend": "snpe",
    "config": {
      "use_buffer": "true",
      "shared_input": "camera_feed"
    }
  }
]
```

### 9.3 生命周期管理

| 事件 | 引用计数 | 内存状态 |
|------|---------|---------|
| model_a.Load() | 1 | `posix_memalign` 分配 |
| model_b.Load() | 2 | 复用同一指针 |
| model_a.Unload() | 1 | 仍在共享池中 |
| model_b.Unload() | 0 | 移至 free list（不释放，可复用） |
| context 析构 | - | 全部释放 |

> **注意**：共享缓冲的模型**推理必须串行执行**（或在外部自行同步），因为 SNPE 在执行期间直接读写该内存。

### 9.4 内存收益

| 配置 | 3 模型独立分配 | 3 模型共享缓冲 |
|------|--------------|---------------|
| 720p 输入内存 | 3 × 2.76MB = 8.28MB | **2.76MB** |
| 输入 memcpy 次数 | 3 次 | **1 次**（写入共享缓冲） |
| posix_memalign 调用 | 3 次 | **1 次** |

---

## 10. 完整示例

### 10.1 单模型 UserBuffer + 量化适配

```cpp
#include "atlas/atlas.h"
#include <cstring>
#include <cassert>

int main() {
    // 初始化
    atlas::AtlasRuntime runtime;
    auto ret = runtime.Init("manifest.json");
    assert(ret == atlas::utils::ErrorCode::kOk);

    // 获取模型
    auto model = runtime.GetModel("detection");
    assert(model.IsValid());

    // 查看模型信息
    auto in_info = model.GetInputInfoAt(0);
    printf("Input: %s shape=[", in_info.name.c_str());
    for (auto d : in_info.shape) printf("%d ", d);
    printf("] dtype=%d layout=%s\n",
           static_cast<int>(in_info.dtype),
           in_info.layout.c_str());

    // Level 2：零拷贝输入
    auto input_tensor = model.GetInputTensor(0);
    assert(input_tensor.data != nullptr);

    // 模拟加载 U8 图像数据
    uint8_t image[3 * 640 * 640];
    // ... 填充图像数据 ...

    // 直接写入内部缓冲（零拷贝）
    std::memcpy(input_tensor.data, image, input_tensor.byte_size);

    // 推理
    std::vector<atlas::utils::Tensor> outputs;
    ret = model.Run(input_tensor, &outputs);
    assert(ret == atlas::utils::ErrorCode::kOk);

    // 处理输出
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto& out = outputs[i];
        printf("Output[%zu]: %s byte_size=%zu\n",
               i, out.info.name.c_str(), out.byte_size);
    }

    return 0;
}
```

对应的 `manifest.json`：

```json
{
  "version": "1.0",
  "name": "snpe-user-buffer-demo",
  "models": [
    {
      "id": "detection",
      "backend": "snpe",
      "model_path": "${MODEL_DIR}/yolo_quantized.dlc",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true",
        "performance_profile": "high_performance"
      },
      "inputs": [
        {
          "name": "data",
          "shape": [1, 3, 640, 640],
          "dtype": "uint8",
          "layout": "NCHW"
        }
      ]
    }
  ]
}
```

### 10.2 双模型共享输入

```cpp
#include "atlas/atlas.h"
#include <cstring>
#include <cassert>

int main() {
    atlas::AtlasRuntime runtime;
    auto ret = runtime.Init("manifest.json");
    assert(ret == atlas::utils::ErrorCode::kOk);

    // 获取两个模型的句柄
    auto model_a = runtime.GetModel("model_a");
    auto model_b = runtime.GetModel("model_b");
    assert(model_a.IsValid() && model_b.IsValid());

    // 获取两个模型的输入 Tensor（内部缓冲指向同一物理内存）
    auto input_a = model_a.GetInputTensor(0);
    auto input_b = model_b.GetInputTensor(0);

    // 验证共享：两个缓冲指向同一地址
    assert(input_a.data == input_b.data);
    printf("Shared buffer at %p, size=%zu\n",
           input_a.data, input_a.byte_size);

    // 只需写入一次
    uint8_t image[3 * 640 * 640];
    // ... 填充图像数据 ...
    std::memcpy(input_a.data, image, input_a.byte_size);

    // 串行推理
    std::vector<atlas::utils::Tensor> outputs;
    ret = model_a.Run(input_a, &outputs);
    assert(ret == atlas::utils::ErrorCode::kOk);

    ret = model_b.Run(input_b, &outputs);
    assert(ret == atlas::utils::ErrorCode::kOk);

    return 0;
}
```

对应 manifest：

```json
{
  "version": "1.0",
  "name": "snpe-shared-buffer-demo",
  "models": [
    {
      "id": "model_a",
      "backend": "snpe",
      "model_path": "${MODEL_DIR}/model_a.dlc",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true",
        "shared_input": "camera_feed"
      }
    },
    {
      "id": "model_b",
      "backend": "snpe",
      "model_path": "${MODEL_DIR}/model_b.dlc",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true",
        "shared_input": "camera_feed"
      }
    }
  ]
}
```

### 10.3 SetInputBuffer 注册外部源

演示 `SetInputBuffer` 的正确用法——当外部数据源已经存在于某块内存中（如摄像头 DMA 缓冲区），通过注册避免手动 memcpy，但内部仍有一次拷贝。

```cpp
#include "atlas/atlas.h"
#include <cstring>
#include <cassert>

// 外部数据源（如摄像头 ISP 直接输出的 DMA 缓冲区）
size_t frame_size = 3 * 640 * 640;  // U8, 1 byte per pixel
uint8_t* dma_buffer = /* from camera ISP */;

int main() {
    atlas::AtlasRuntime runtime;
    auto ret = runtime.Init("manifest.json");
    assert(ret == atlas::utils::ErrorCode::kOk);

    auto model = runtime.GetModel("detection");
    assert(model.IsValid());

    // 注册外部数据源（仅 use_buffer=true 时有效）
    ret = model.SetInputBuffer(0, dma_buffer, frame_size);
    assert(ret == atlas::utils::ErrorCode::kOk);

    // 推理 — SetInputBuffer 模式下，t.data 会被忽略
    // 后端从 dma_buffer memcpy 到内部 user_input_raw 再执行
    atlas::utils::Tensor dummy;
    std::vector<atlas::utils::Tensor> outputs;
    ret = model.Run(dummy, &outputs);
    assert(ret == atlas::utils::ErrorCode::kOk);

    // 重置为内部预分配缓冲
    model.SetInputBuffer(0, nullptr, 0);

    return 0;
}
```

> **与 Level 2 的对比**：`SetInputBuffer` 只提供了"调用方无需手动传入数据"的便利，内部仍有 1 次 memcpy。
> 真正的零拷贝请使用 [10.1 节](#101-单模型-userbuffer--量化适配) 的 Level 2 方式（`GetInputTensor`）。

---

## 11. 常见问题与排查

### 11.1 "SetInputBuffer requires use_buffer=true"

```
Error: SetInputBuffer requires use_buffer=true
```

**原因**：在 ITensor 模式下调用了 `SetInputBuffer()`。

**解决**：manifest 中设置 `"use_buffer": "true"`：

```json
"config": { "use_buffer": "true" }
```

### 11.2 推理结果完全错误（输入范围问题）

**现象**：量化模型推理结果明显错误（如检测框位置完全不对）。

**可能原因**：TF8 模型接收了 U8 输入但未做适配。

**排查步骤**：

1. 检查模型量化类型：
   ```bash
   snpe-dlc-info -i model.dlc | grep -i quant
   ```

2. 确认 manifest 中声明了输入 dtype：
   ```json
   "inputs": [{ "name": "data", "dtype": "uint8" }]
   ```

3. 确认传入 Tensor 的 dtype：
   ```cpp
   input_tensor.info.dtype = atlas::utils::DataType::kUInt8;
   ```

### 11.3 GetInputTensor 返回空指针

**原因**：后端不支持零拷贝输入。

**排查**：

- 检查 `GetInputBuffer(size_t index)` 是否返回非空 `Span`
- 确认 `use_buffer` 配置正确

### 11.4 UserBuffer 创建失败

**症状**：`Load()` 返回 `kInferFailed`

**可能原因**：

1. **不支持的 dtype**：`CreateEncoding()` 返回 nullptr，说明当前 dtype 不被 SNPE 版本支持（参照 [4.3 节](#43-userbuffer-支持的数据类型)）

   | SNPE 版本 | 支持编码 |
   |-----------|---------|
   | 1.x | `UserBufferEncodingFloat`, `UserBufferEncodingTfN(zp,scale,8)`, `UserBufferEncodingTfN(zp,scale,16)` |
   | 2.x | + `UserBufferEncodingFloat16`, `UserBufferEncodingUint8`, `UserBufferEncodingInt32` |

2. **内存分配失败**：`posix_memalign` 返回非零

   **解决**：检查系统内存，减少对齐大小（默认 128 字节）

### 11.5 DSP 运行时 UserBuffer 问题

| 问题 | 解决 |
|------|------|
| 运行时崩溃 | 确保内存 128 字节对齐（`AlignedBuffer` 默认） |
| 性能未达预期 | 检查 stride 是否为 element_size 整数倍 |
| DSP 未生效 | 确认 runtime 设为 `"dsp"`，且设备支持 DSP |

### 11.6 共享缓冲的线程安全性

**问题**：多个模型共享同一输入缓冲时，同时推理导致数据竞争。

**规则**：
- 共享同一 `shared_input` key 的模型**必须串行执行** `Run()`
- 如果需要在不同线程中推理，每个线程需要独立的输入缓冲（不设置 `shared_input`）
- `SnpeMemoryPool` 本身是线程安全的（`AcquireShared`/`ReleaseShared` 使用 mutex 保护）
- 池操作仅在 `Load()`/`Unload()` 中调用，推理热路径无锁

### 11.7 如何验证量化适配生效

通过日志确认适配是否被触发：

```bash
# 启用 atlas debug 日志
export ATLAS_LOG_LEVEL=2  # ATLAS_LOG_LEVEL_DEBUG

# 日志中可以看到：
# InferWithBuffer() — NeedsQuantizationAdaptation true for input[0]
# AdaptU8ToTf8 called: count=1228800 bytes
```

---

## 附录 A：SNPE SDK 参考示例及关键流程对比

### A.0 示例文件路径与模块简介

参考路径：
- **v1 (1.50.0.2622)**: `/opt/qcom/sdk/snpe-1.50.0.2622/examples/NativeCpp/SampleCode/jni/`
- **v2 (2.21.0.240401)**: `/opt/qcom/aistack/qairt/2.21.0.240401/examples/SNPE/NativeCpp/SampleCode/jni/`

两版本文件结构一致，各模块功能如下：

| 文件 | 模块 | 说明 |
|------|------|------|
| `main.cpp` | 主入口 | 命令行参数解析、buffer 类型选择、推理流程路由（ITensor vs UserBuffer 分支） |
| `SetBuilderOptions.cpp` | 构建 SNPE 对象 | 封装 `SNPEBuilder` 构造流程，包括 runtime 顺序、UserBuffer 开关、缓存初始化等 |
| `CreateUserBuffer.cpp` | UserBuffer 创建 | 从 `IBufferAttributes` 获取 tensor 元数据，计算 stride，创建 `IUserBuffer` 并注册到 `UserBufferMap` |
| `LoadInputTensor.cpp` | 输入数据加载 | 浮点模式：从文件读取 float 数据填入 ITensor；TFN 模式：从文件读取量化数据，运行时设定 scale/zp |
| `SaveOutputTensor.cpp` | 输出数据保存 | 将推理结果写入文件，支持 float 和 TfN 两种格式 |
| `LoadContainer.cpp` | DLC 容器加载 | 从文件路径加载 `.dlc` 模型容器 |
| `CheckRuntime.cpp` | 运行时检测 | 检查目标 runtime（CPU/GPU/DSP）是否可用 |
| `PreprocessInput.cpp` | 输入预处理 | 解析输入文件列表，按 batch size 分组 |
| `Util.cpp` | 工具函数 | `calcSizeFromDims`、`loadByteDataFileBatched`、`loadByteDataFileBatchedTfN` 等实用函数 |
| `NV21Load.cpp` | NV21 格式加载 | 从 NV21 文件加载图像数据 |

> 以上示例可直接编译运行 `snpe-sample`（SNPE SDK 自带），用于验证 SNPE 原生 API 的正确行为，再对比 atlas-runtime 的实现是否一致。

### A.1 构建 SNPE 对象

两版本均在 `SetBuilderOptions.cpp` 中构建，主要差异：

| 环节 | v1 (1.50.0) | v2 (2.21.0) |
|------|-------------|-------------|
| 输出层配置 | `setOutputLayers({})` | `setOutputTensorNames({})` |
| CPU 定点模式 | ❌ 不支持 | ✅ `setCpuFixedPointMode(cpuFixedPointMode)` |
| 缓存加速 | `setInitCacheMode(useCaching)` | 同上 |

**v1**:
```cpp
snpe = snpeBuilder.setOutputLayers({})
   .setRuntimeProcessorOrder(runtimeList)
   .setUseUserSuppliedBuffers(useUserSuppliedBuffers)
   .setPlatformConfig(platformConfig)
   .setInitCacheMode(useCaching)
   .build();
```

**v2**:
```cpp
snpe = snpeBuilder.setOutputTensorNames({})
   .setRuntimeProcessorOrder(runtimeList)
   .setUseUserSuppliedBuffers(useUserSuppliedBuffers)
   .setPlatformConfig(platformConfig)
   .setInitCacheMode(useCaching)
   .setCpuFixedPointMode(cpuFixedPointMode)
   .build();
```

### A.2 UserBuffer 创建流程

v1 与 v2 均通过 `IUserBufferFactory::createUserBuffer()` 创建 UserBuffer，但参数来源不同：

**v1** — 硬编码量化参数：
```cpp
// CreateUserBuffer.cpp (v1.50.0)
if (isTfNBuffer) {
    // 硬编码 stepExactly0=0, quantizedStepSize=1.0
    userBufferEncoding = std::make_unique<UserBufferEncodingTfN>(
        0, 1.0f, bitWidth);   // bitWidth=8(TF8)/16(TF16)
} else {
    userBufferEncoding = std::make_unique<UserBufferEncodingFloat>();
}
auto& ubFactory = SNPEFactory::getUserBufferFactory();
snpeUserBackedBuffers.push_back(ubFactory.createUserBuffer(
    applicationBuffers.at(name).data(),   // void* buffer
    bufSize,                              // size_t size
    strides,                              // vector<size_t> strides
    userBufferEncoding.get()));           // UserBufferEncoding*
```

**v2** — 支持从模型读取静态量化参数：
```cpp
// CreateUserBuffer.cpp (v2.21.0)
if (isTfNBuffer) {
    // 新增 staticQuantization 参数
    if (encodingType == FLOAT && staticQuantization) {
        // 模型无量化参数时报错退出
    }
    // 从模型 IBufferAttributes 读取真实 scale/zp
    auto* ubeTfN = dynamic_cast<UserBufferEncodingTfN*>(
        attrs->getEncoding());
    uint64_t stepExactly0 = ubeTfN->getStepExactly0();
    float quantizedStepSize = ubeTfN->getQuantizedStepSize();
    userBufferEncoding = std::make_unique<UserBufferEncodingTfN>(
        stepExactly0, quantizedStepSize, bitWidth);
} else {
    userBufferEncoding = std::make_unique<UserBufferEncodingFloat>();
}
```

### A.3 类型选择与 bitWidth

两版本 main.cpp 的 buffer 类型选择逻辑一致：

```cpp
enum {UNKNOWN, USERBUFFER_FLOAT, USERBUFFER_TF8, ITENSOR, USERBUFFER_TF16};

if (bufferTypeStr == "USERBUFFER_FLOAT") {
    bufferType = USERBUFFER_FLOAT;
} else if (bufferTypeStr == "USERBUFFER_TF8") {
    bufferType = USERBUFFER_TF8;
    bitWidth = 8;
} else if (bufferTypeStr == "USERBUFFER_TF16") {
    bufferType = USERBUFFER_TF16;
    bitWidth = 16;
} else if (bufferTypeStr == "ITENSOR") {
    bufferType = ITENSOR;
}
bool useUserSuppliedBuffers =
    (bufferType == USERBUFFER_FLOAT ||
     bufferType == USERBUFFER_TF8 ||
     bufferType == USERBUFFER_TF16);
```

### A.4 推理执行路径

| 模式 | v1 | v2 |
|------|----|----|
| ITensor | `snpe->execute(itensor, outputMap)` | 同上 |
| UserBuffer Float | `snpe->execute(inputMap, outputMap)` | 同上 |
| UserBuffer TF8 | 同上 + 运行时写入 TfN 数据 | 同上 + 支持静态量化参数 |
| UserBuffer TF16 | 同上 (bitWidth=16) | 同上 |

### A.5 atlas-runtime 实现与 SDK 的对应

| SDK 流程 | atlas-runtime 对应实现 |
|----------|----------------------|
| `setUseUserSuppliedBuffers(use_buffer)` | `src/backend/snpe/snpe_backend_v1.cc:375` / `v2.cc:362` |
| `SNPEFactory::getUserBufferFactory()` | v1 使用工厂创建 UserBuffer (v1.cc:536-545) |
| `snpe->createInputBuffer()` | v2 使用 SNPE 对象方法 (v2.cc:521-525) |
| `getInputOutputBufferAttributes()` | `BuildTensorInfos()` 中统一采集 (v1.cc:932-976) |
| `UserBufferEncodingTfN(zp, scale, bw)` | `CreateEncoding()` 根据 dtype 自动创建 |
| 应用数据写入 + `execute()` | `InferWithBuffer()` 三步流程 |

## 附录 B：SnpeMemoryPool 工作原理

`SnpeMemoryPool` 是 SNPE backend 内部的线程安全对齐内存池，提供两种复用模式：

1. **Free-list 复用**：`Acquire()` / `Release()` — Load/Unload 周期内的内存复用
2. **共享缓冲**：`AcquireShared(key)` / `ReleaseShared(key)` — 多模型跨生命周期共享

```
池结构:
┌──────────────────────────────────┐
│  SnpeMemoryPool                  │
│                                  │
│  free_list_:                     │
│    [buf_32_bytes] → [buf_128_bytes] → ...
│                                  │
│  shared_buffers_:                │
│    "cam" → { ptr=A, size=N, rc=2 }  │
│    "feat" → { ptr=B, size=M, rc=1 } │
│                                  │
│  total_allocated_: 12345678      │
└──────────────────────────────────┘
```

完整 API 见 `src/backend/snpe/snpe_memory_pool.h`。

---

## 附录 C：ITensor 与 UserBuffer 内部数据流对比

```
ITensor 路径:
  Run(input) →
    Pipeline 预处理 → preprocessed Tensor →
    InferWithTensor():
      1. memcpy(preprocessed.data → ITensor)   ← 1次拷贝
      2. snpe->execute(ITensor, TensorMap)
      3. 返回 TensorMap 指针 (零拷贝)

UserBuffer 路径:
  Run(input) →
    Pipeline 预处理 → preprocessed Tensor →
    InferWithBuffer():
      1. 检测源:
         - 外部注入 (SetInputBuffer)? → memcpy 到 UserBuffer
         - 零拷贝 (GetInputBuffer)?  → skip
         - 量化适配 (U8→TF8)?        → 逐元素转换
         - 默认                      → memcpy
      2. snpe->execute(UserBufferMap, UserBufferMap)
      3. 返回 AlignedBuffer 指针 (零拷贝)
```

---

## 附录 D：QuantParams 与 UserBufferEncoding 映射

| 场景 | QuantParams | UserBufferEncoding 子类 |
|------|------------|----------------------|
| 浮点模型 | `{1.0, 0, 8}` | `UserBufferEncodingFloat` |
| TF8 量化模型 | `{scale, zp, 8}` | `UserBufferEncodingTfN(zp, scale, 8)` |
| U8 输入覆盖 | `{1.0, 0, 8}` (重置) | `UserBufferEncodingTfN(0, 1.0f, 8)` |
| Float16 模型 | — | `UserBufferEncodingFloat16` (2.x only) |

> 开发者无需手动创建 `UserBufferEncoding`。后端在 `Load()` 中根据 `BuildTensorInfos()` 采集的信息自动创建。