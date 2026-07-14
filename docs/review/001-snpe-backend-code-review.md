# SNPE Backend 代码审查报告

> **版本**: 1.0 / **日期**: 2026-07-14 / **审查范围**: `src/backend/snpe/` 全部后端代码
>
> **参考依据**:
> - SNPE SDK v1.50.0.2622 NativeCpp/SampleCode (`/opt/qcom/sdk/snpe-1.50.0.2622/examples/NativeCpp/SampleCode/jni/`)
> - SNPE SDK v2.21.0.240401 NativeCpp/SampleCode (`/opt/qcom/aistack/qairt/2.21.0.240401/examples/SNPE/NativeCpp/SampleCode/jni/`)
> - `docs/snpe_quantization_dev_guide.md` 开发指南
> - `docs/proposals/015-snpe-user-buffer-adaptation.md`

---

## 目录

1. [概述](#1-概述)
2. [发现的问题](#2-发现的问题)
3. [SDK 实现与 atlas 实现的差异分析](#3-sdk-实现与-atlas-实现的差异分析)
4. [改进建议及解决方案](#4-改进建议及解决方案)
5. [修复计划](#5-修复计划)
6. [总结](#6-总结)

---

## 1. 概述

本报告基于两个 SNPE SDK 版本的 NativeCpp 官方示例，对 atlas-runtime `src/backend/snpe/` 目录下的后端实现代码进行逐项审查，识别问题、差异及改进方向。

### 审查范围

| 文件 | 行数 | 角色 |
|------|------|------|
| `snpe_backend_v1.cc` | 1026 | SNPE 1.x 后端完整实现 |
| `snpe_backend_v2.cc` | 983 | SNPE 2.x 后端完整实现 |
| `snpe_backend.h` | 99 | 后端公开接口 + QuantParams 结构体 |
| `snpe_backend_context.h` | 47 | 共享上下文 + SnpeMemoryPool |
| `snpe_aligned_buffer.h` | 83 | 对齐内存 RAII 包装 |
| `snpe_backend_stub.cc` | — | 无 SDK 环境桩实现 |

---

## 2. 发现的问题

### 2.1 BUG: v1.ParseDataType() 将 TF16 错误映射为 kUInt8（严重）

**位置**: `snpe_backend_v1.cc:148-158`

```cpp
case zdl::DlSystem::UserBufferEncoding::ElementType_t::TF16:
    return utils::DataType::kUInt8;   // ← 错误：应为 kFloat16
```

**影响**: 当加载 TF16 量化模型时，运行时报告的 SNPE `ElementType_t::TF16` 被映射为 `kUInt8`，然后 `CreateEncoding(kUInt8)` 硬编码 `TfN(0, 1.0f, 8)`（固定 bitWidth=8），丢失了 TF16 应有的 `bitWidth=16`。导致：

1. 模型实际 TF16 量化信息丢失
2. 无法正确创建 UserBufferEncoding
3. 推理结果可能错误

**SDK 一致性**: v2 的 `ParseDataType` 正确处理了 TF16（`ElementType_t::FLOAT16` → `kFloat16` → `UserBufferEncodingFloat16`）。

**严重性**: ⚠️ 中。TF16 模型在 v1 中不可用（退化行为）。

### 2.2 设计问题: SetInputBuffer() 被误称为"外部内存注入"（中等）

**位置**: `snpe_backend_v1.cc:899-922`

当前实现中 `SetInputBuffer()` 仅在 `user_input_external[i]` 中存储指针，推理时 `InferWithBuffer()` 调用 `memcpy(target, external_data, size)` 将外部数据拷贝到内部 `user_input_raw`。**这不是零拷贝**——仍有 1 次 memcpy。

```cpp
// InferWithBuffer() 第 709-713 行
if (impl_->user_input_external[i] != nullptr &&
    t.data == impl_->user_input_external[i]) {
    std::memcpy(target, t.data, std::min(t.byte_size, target_size));  // 1 次拷贝
    continue;
}
```

**根因**: UserBuffer 在 `Load()` 创建时即绑定到 `user_input_raw[i].data`（内部对齐内存），`IUserBuffer` 的 data 指针不可在运行时修改。SDK 没有提供 `setBufferAddress()` 接口。

**SDK 参考**: SDK 示例中也没有类似 `SetInputBuffer()` 的接口——所有数据都直接写入 `applicationBuffers`（用户预分配的 `std::vector<uint8_t>`），不存在"注册外部源"的概念。

### 2.3 一致性问题: v1 使用工厂创建 UserBuffer，v2 使用 SNPE 对象方法（轻度）

**v1**:
```cpp
auto& ub_factory = zdl::SNPE::SNPEFactory::getUserBufferFactory();
auto user_buf = ub_factory.createUserBuffer(raw.data, size, stride_shape, encoding.get());
```

**v2**:
```cpp
auto user_buf = impl_->snpe->createInputBuffer(name.c_str(), size, stride.data(), encoding.get());
```

两个用法都正确但签名不同。v1 使用工厂模式接受 `TensorShape` 作为 stride 参数，v2 使用 `SNPE::createInputBuffer()` 接受 `vector<size_t>` 作为 stride 参数。当前实现已正确区分。

### 2.4 缺失功能: v2 不支持 setCpuFixedPointMode（轻度）

**位置**: `snpe_backend_v2.cc:359-364`

SDK v2.21.0 的 `SNPEBuilder` 支持 `setCpuFixedPointMode()`，允许在 CPU runtime 上以定点模式（int8）执行量化 DLC。当前 v2 后端未使用此配置项。

```cpp
// SDK v2.21.0 SetBuilderOptions.cpp
snpe = snpeBuilder.setOutputTensorNames({})
   .setRuntimeProcessorOrder(runtimeList)
   .setUseUserSuppliedBuffers(useUserSuppliedBuffers)
   .setPlatformConfig(platformConfig)
   .setInitCacheMode(useCaching)
   .setCpuFixedPointMode(cpuFixedPointMode)   // ← atlas 未暴露
   .build();
```

**影响**: 用户在 CPU runtime 上运行量化模型时，无法通过 manifest 控制是否启用定点执行模式。

### 2.5 BUILD 粒度: 无单独编译单元（轻微）

`snpe_backend_v1.cc` 和 `snpe_backend_v2.cc` 各自包含了完整的 `CreateEncoding()`、`ParseDataType()`、`ComputeUserBufferStride()`、`AdaptU8ToTf8()`、`NeedsQuantizationAdaptation()`、`ExtractQuantParamsFromAttrs()`、`QuantParamsForDtype()` 等辅助函数，v1 和 v2 之间没有代码复用。

这些函数中有大量逻辑相同（如 `AdaptU8ToTf8`、`ComputeUserBufferStride`、`NeedsQuantizationAdaptation`），但被复制粘贴了两份。

---

## 3. SDK 实现与 atlas 实现的差异分析

### 3.1 创建 UserBuffer 时的量化参数来源

| 环节 | SDK v1.50.0 | SDK v2.21.0 | atlas v1 | atlas v2 |
|------|-------------|-------------|----------|----------|
| 获取 tensor 属性 | `getInputOutputBufferAttributes(name)` | 同上 | `BuildTensorInfos()` | 同上 |
| 读取 scale/zp | 运行时从文件数据提取 | 从 IBufferAttributes 读取 + 文件数据 | 从 IBufferAttributes 提取 | 从 IBufferAttributes 提取 |
| 创建 encoding | `TfN(0,1.0,bitWidth)` 硬编码 | `TfN(stepExactly0, quantizedStepSize, bitWidth)` 静态/动态 | `TfN(zp,scale,bw)` 从 attrs 提取 | `TfN(zp,scale,bw)` 从 attrs 提取 |
| 写入数据方式 | `loadByteDataFileBatchedTfN()` 直接写入 applicationBuffers | 同上 + `dynamic_cast` 更新 encoding 参数 | `memcpy`/`AdaptU8ToTf8` 写入 user_input_raw | 同上 |

**差异点**: atlas 在 `BuildTensorInfos()` 中预先从 `IBufferAttributes` 提取量化参数存储为 `QuantParams`，然后在 `Load()` 中创建 `UserBufferEncodingTfN(zp, scale, bw)`。SDK v1 的做法是硬编码 `TfN(0, 1.0, bitWidth)`，然后在运行时通过 `loadInputUserBufferTfN()` 中的 `dynamic_cast` 修改 encoding 的 scale/zp。

atlas 的做法（预提取+预先创建）实际上比 SDK v1 更合理，因为：
1. 避免了运行时 `dynamic_cast` 的开销
2. 更清晰地分离了"元数据采集"和"推理执行"两个阶段

### 3.2 SDK 中变更 scale/zp 的路径

SDK v1.50.0 的 `loadInputUserBufferTfN()` 展示了另一个可能的路径——**运行时变更 encoding 参数**：

```cpp
// SDK v1.50.0 LoadInputTensor.cpp
auto userBufferEncoding = dynamic_cast<zdl::DlSystem::UserBufferEncodingTfN*>(
    &inputMap.getUserBuffer(name)->getEncoding());
userBufferEncoding->setStepExactly0(stepEquivalentTo0);
userBufferEncoding->setQuantizedStepSize(quantizedStepSize);
```

atlas 当前没有支持此模式——quant params 在 `Load()` 中一次性确定，`Infer()` 阶段不再修改。

### 3.3 输出格式控制

| 环节 | SDK | atlas |
|------|-----|-------|
| 输出 UserBuffer 创建 | 同输入一样根据 `isTfNBuffer` 选择 Float/TfN | 根据 `manifest outputs[].dtype` 或模型 runtime 报告 |
| 输出格式覆盖 | 不支持（创建时已固定） | 支持（通过 manifest 的 `outputs[].dtype`） |

atlas 在此方面比 SDK 示例提供了更灵活的输出格式控制能力。

### 3.4 infer 执行路径对比

| 模式 | SDK 示例 | atlas |
|------|---------|-------|
| ITensor | `snpe->execute(inputTensor, outputTensorMap)` | `snpe->execute(itensor, output_map)` |
| UserBuffer Float | `snpe->execute(inputMap, outputMap)` | `snpe->execute(input_map, output_map)` |
| UserBuffer TfN | `snpe->execute(inputMap, outputMap)` + 运行时写数据 | `snpe->execute(input_map, output_map)` + 预处理 memcpy/adapt |
| 输出写入 | 浮点: float 文件; TfN: 字节文件 | Tensor.data 指针零拷贝 |

执行路径一致，无明显差异。

---

## 4. 改进建议及解决方案

### 4.1 修复 v1 TF16 映射（高优先级）

#### 问题代码

`snpe_backend_v1.cc:153` — `ParseDataType()` 中 `TF16` 被映射为 `kUInt8`。

#### 解决步骤

**步骤 1**: 修改 `ParseDataType()`，将 `TF16` 映射到正确的 `kFloat16`。

```cpp
// snpe_backend_v1.cc — ParseDataType()
case zdl::DlSystem::UserBufferEncoding::ElementType_t::TF16:
    return utils::DataType::kFloat16;   // 修复：原为 kUInt8
```

**步骤 2**: 在 `CreateEncoding()` 中增加 `kFloat16` 分支。v1 没有 `UserBufferEncodingFloat16`，因此使用 `UserBufferEncodingTfN(zp, scale, 16)` 实现，与 SDK v1.50.0 官方示例一致。

```cpp
// snpe_backend_v1.cc — CreateEncoding()
case utils::DataType::kFloat16:
    // v1 通过 TfN 模拟 float16，bandwidth=16
    return std::make_unique<zdl::DlSystem::UserBufferEncodingTfN>(
        zero_point, scale, bandwidth);
```

**步骤 3**: 同步更新 `QuantParamsForDtype()`，为 `kFloat16` 设置合理的默认量化参数。

```cpp
// snpe_backend_v1.cc — QuantParamsForDtype()
SnpeBackend::QuantParams QuantParamsForDtype(utils::DataType dtype) {
    SnpeBackend::QuantParams qp;
    if (dtype == utils::DataType::kInt8) {
        qp = {1.0f, 0, 8};
    } else if (dtype == utils::DataType::kFloat16) {
        qp = {1.0f, 0, 16};  // bandwidth=16
    }
    return qp;
}
```

**影响范围**: 仅 `snpe_backend_v1.cc`，3 处改动，约 5 行代码。

#### 验证方法

| 验证项 | 方法 | 预期 |
|--------|------|------|
| ParseDataType 正确性 | 构造 TF16 `ElementType_t` 输入调用 | 返回 `kFloat16` |
| CreateEncoding 正确性 | 调用 `CreateEncoding(kFloat16, 1.0, 0, 16)` | 返回 `UserBufferEncodingTfN(0, 1.0, 16)` |
| 端到端 TF16 模型加载 | 使用 TF16 量化 DLC 执行 `Load()` + `Infer()` | 成功，结果正确 |
| 无回归 | 现有测试套件全量运行 | 全部通过 |

### 4.2 暴露 v2 setCpuFixedPointMode（中优先级）

#### 问题代码

`snpe_backend_v2.cc:359-364` — builder 链中缺失 `setCpuFixedPointMode()`。

#### 解决步骤

**步骤 1**: 定义配置键常量。

```cpp
// snpe_backend_v2.cc 匿名 namespace 中
constexpr char kConfigCpuFixedPoint[] = "cpu_fixed_point";
```

**步骤 2**: 在 `Load()` 中提取配置项。

```cpp
// snpe_backend_v2.cc — Load() 步骤 2，插入在 use_buffer 解析之后
bool cpu_fixed_point = false;
{
    auto it = config.config.find(kConfigCpuFixedPoint);
    if (it != config.config.end() && it->second == "true") {
        cpu_fixed_point = true;
    }
}
```

**步骤 3**: 在 builder 链中调用 `setCpuFixedPointMode()`。

```cpp
// snpe_backend_v2.cc — 步骤 4 builder 链
SNPE::SNPEBuilder builder(impl_->container.get());
builder.setRuntimeProcessorOrder(runtime_list);
builder.setPerformanceProfile(perf_profile);
builder.setUseUserSuppliedBuffers(use_buffer);
builder.setPlatformConfig(platform_config);
builder.setCPUFallbackMode(false);
if (cpu_fixed_point) {
    builder.setCpuFixedPointMode(true);
}
```

**步骤 4**: manifest 配置示例。

```json
{
  "models": [
    {
      "id": "my_model",
      "backend": "snpe",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true",
        "cpu_fixed_point": "true"
      }
    }
  ]
}
```

**影响范围**: 仅 `snpe_backend_v2.cc` + 文档更新，约 10 行代码。

#### 验证方法

| 验证项 | 方法 | 预期 |
|--------|------|------|
| 配置解析 | config 含 `cpu_fixed_point=true` | `cpu_fixed_point` 变量为 true |
| builder 调用 | 配置为 true 时 builder 链包含该调用 | 日志输出确认 |
| 不配置时无影响 | config 不含该键 | 行为与之前完全相同 |
| CPU 定点执行 | CPU runtime + 量化 DLC + `true` | 推理成功，输出为定点结果 |

### 4.3 修改 SetInputBuffer 语义文档（低优先级）

#### 问题代码

`snpe_backend_v1.cc:899-922` — `SetInputBuffer()` 仅注册外部指针，推理时仍有 1 次 memcpy。

#### 当前解决方案（已实施）

修正 `docs/snpe_quantization_dev_guide.md` 中关于 Level 3 的描述，明确标注"不是零拷贝，仍有 1 次 memcpy"。

#### 未来零拷贝方案（当 SNPE SDK 支持时）

**方案一**: 如果未来 SNPE SDK 提供 `IUserBuffer::setBufferAddress()` 方法，可在 `SetInputBuffer()` 中直接调用，将 IUserBuffer 的 data 指针指向外部内存。

```cpp
// 未来可能的实现（依赖 SDK 版本提供 setBufferAddress 方法）
utils::ErrorCode SnpeBackend::SetInputBuffer(size_t index,
                                               void* external_mem,
                                               size_t byte_size) {
    // ... 校验 ...
    auto* ub = impl_->user_input_buffers[index].get();
    // TODO: 如果 SNPE SDK 支持 setBufferAddress()
    // ub->setBufferAddress(external_mem);
    return utils::ErrorCode::kOk;
}
```

**方案二**: 改 `Load()` 阶段直接使用外部内存创建 IUserBuffer（需在 Load 时已知外部源）。

```cpp
// 未来可能的实现：Load() 时直接绑定外部内存
// 需 manifest 中声明 external_buffer=true + external_buffer_path
void* external_mem = get_external_buffer_by_path(path);
auto user_buf = ub_factory.createUserBuffer(
    external_mem,  // 直接使用外部内存创建 UserBuffer
    size, stride_shape, encoding.get());
```

**当前不建议做任何代码改动**，因为 SDK 接口限制。

### 4.4 代码复用 — 提取公共工具函数（低优先级）

#### 问题代码

`snpe_backend_v1.cc` 和 `snpe_backend_v2.cc` 各自复制了以下函数：
- `AdaptU8ToTf8()`
- `ComputeUserBufferStride()`
- `NeedsQuantizationAdaptation()`
- `kBufferAlignment` 常量

#### 解决步骤

**步骤 1**: 新建 `src/backend/snpe/snpe_common.h`。

```cpp
// src/backend/snpe/snpe_common.h — 新增
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlas {
namespace backend {

// DSP/HTP buffer alignment requirement.
constexpr size_t kBufferAlignment = 128;

// Computes per-dimension byte strides for a UserBuffer.
inline std::vector<size_t> ComputeUserBufferStride(
    const std::vector<int>& shape, size_t element_size) {
    const size_t rank = shape.size();
    if (rank == 0) return {};
    std::vector<size_t> stride(rank, element_size);
    for (size_t i = rank; i > 1; --i) {
        stride[i - 2] = stride[i - 1] *
            static_cast<size_t>(std::max(shape[i - 1], 1));
    }
    return stride;
}

// Adapts uint8_t input data to int8_t (TF8) by subtracting 128.
inline void AdaptU8ToTf8(const void* src, void* dst, size_t count) {
    const auto* u8_src = static_cast<const uint8_t*>(src);
    auto* s8_dst = static_cast<int8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        s8_dst[i] = static_cast<int8_t>(static_cast<int>(u8_src[i]) - 128);
    }
}

// Returns true if the input at |index| needs U8->TF8 conversion.
inline bool NeedsQuantizationAdaptation(
    const std::vector<utils::TensorInfo>& info,
    size_t index, utils::DataType input_dtype) {
    return (info[index].dtype == utils::DataType::kInt8 &&
            input_dtype == utils::DataType::kUInt8);
}

}  // namespace backend
}  // namespace atlas
```

**步骤 2**: 从 v1.cc 和 v2.cc 中删除重复的函数定义和 `kBufferAlignment` 常量，改为 `#include "src/backend/snpe/snpe_common.h"`。

**步骤 3**: 更新 `src/backend/snpe/BUILD`，将 `snpe_common.h` 加入 deps（头文件直接引用，无需单独 cc_library）。

**保留不提取的部分**（因存在命名空间差异或实现差异）：

| 函数 | 原因 |
|------|------|
| `CreateEncoding()` | v1/v2 支持的类型不同 |
| `ExtractQuantParamsFromAttrs()` | v1/v2 的 SNPE 类型命名空间不同（`zdl::` vs 无前缀） |
| `QuantParamsForDtype()` | 语义一致但依赖各自的 `ParseDataType()` 映射 |
| `ParseDataType()` | v1/v2 的 ElementType_t 枚举值不同 |
| `ParseRuntime()` / `ParsePerformanceProfile()` | 各自 namespace |

### 4.5 运行时 encoding 参数动态更新（未来功能）

#### 背景

SDK v1.50.0 的 `loadInputUserBufferTfN()` 展示了在推理时通过 `dynamic_cast` 动态更新 UserBuffer 的 scale/zp 参数的能力。

```cpp
// SDK v1.50.0 LoadInputTensor.cpp — 参考模式
auto userBufferEncoding = dynamic_cast<zdl::DlSystem::UserBufferEncodingTfN*>(
    &inputMap.getUserBuffer(name)->getEncoding());
if (userBufferEncoding) {
    userBufferEncoding->setStepExactly0(stepEquivalentTo0);
    userBufferEncoding->setQuantizedStepSize(quantizedStepSize);
}
```

#### 适用场景

每个输入帧的量化参数可能不同的场景（例如摄像头 ISP 输出逐帧变化的量化表），而 atlas 当前在 `Load()` 阶段一次性确定量化参数后不再更新。

#### 实现方案

在 manifest 中增加 `dynamic_quant_params` 配置开关。启用后，在 `InferWithBuffer()` 的 Step 1 中增加动态更新逻辑：

```cpp
// 在 InferWithBuffer() Step 1 填充输入缓冲之前
if (impl_->dynamic_quant_params) {
    for (size_t i = 0; i < inputs.size(); ++i) {
        // 通过 dynamic_cast 更新 input_map 中的 encoding
        auto* encoding = dynamic_cast<DlSystem::UserBufferEncodingTfN*>(
            &impl_->user_input_buffers[i]->getEncoding());
        if (encoding) {
            // 从输入 Tensor 的 info 中读取新的 scale/zp
            encoding->setStepExactly0(inputs[i].info.zero_point);
            encoding->setQuantizedStepSize(inputs[i].info.scale);
        }
    }
}
```

需要先在 `TensorInfo` 中增加 `zero_point` 和 `scale` 字段（当前不存在），且在 manifest 中能够配置每帧的量化参数。

**当前不需要实现**，因为：
1. `TensorInfo` 当前不包含量化参数字段
2. 没有实际业务场景驱动
3. SDK v1 示例只是为演示目的，非必须功能

### 4.6 同步开发指南表格

#### 问题代码

`docs/snpe_quantization_dev_guide.md` 中 11.4 节的版本支持表格写为：
```
1.x | UserBufferEncodingFloat, UserBufferEncodingTfN(zp,scale,8/16)
```

但 atlas v1 当前实际上不支持 TF16（因为 `ParseDataType` 映射错误）。应在修复 4.1 后更新此表格为：
```
1.x | UserBufferEncodingFloat, UserBufferEncodingTfN(zp,scale,8), UserBufferEncodingTfN(zp,scale,16)
```

---

## 5. 修复计划

### 5.1 优先级与批次

| 批次 | 问题 | 工作量 | 风险 | 依赖 |
|------|------|--------|------|------|
| **P0** | 4.1 TF16 映射修复 | ~5 行 | 低 | 无 |
| **P1** | 4.2 setCpuFixedPointMode | ~10 行 | 低 | 无 |
| **P2** | 4.6 同步文档表格 | ~2 行 | 无 | 4.1 |
| **P3** | 4.4 代码复用 | ~30 行 | 中 | 无 |
| **P4** | 4.5 动态 encoding | 未来 | — | TensorInfo 扩展 |

### 5.2 进度跟踪

- [x] **P0: 修复 v1 TF16 映射** — `snpe_backend_v1.cc` 3 处改动
  - `ParseDataType()`: `TF16 → kFloat16`
  - `CreateEncoding()`: 增加 `kFloat16` 分支
  - `QuantParamsForDtype()`: 增加 `kFloat16` 默认参数 `{1.0f, 0, 16}`
- [x] **P1: 暴露 v2 setCpuFixedPointMode** — `snpe_backend_v2.cc`
  - 新增 `kConfigCpuFixedPoint` 常量
  - `Load()` 增加 `cpu_fixed_point` 配置提取 + builder 调用
- [x] **P3: 提取公共工具函数** — 新建 `src/backend/snpe/snpe_common.h`
  - 提取 `kBufferAlignment`、`ComputeUserBufferStride`、`AdaptU8ToTf8`、`NeedsQuantizationAdaptation`
  - 从 v1.cc/v2.cc 删除重复定义，改为 `#include`
  - 更新 BUILD 新增 `:snpe_common` 目标
- [ ] P2: 同步 11.4 节表格（修复 TF16 后更新）
- [ ] P4: 动态 encoding（未来，待业务驱动）

---

## 6. 总结

### 6.1 需修复的问题

| # | 问题 | 文件 | 优先级 | 性质 | 状态 |
|---|------|------|--------|------|------|
| 1 | TF16 映射为 kUInt8 | v1.cc:153 | 🔴 高 | BUG | ✅ 已修复 |
| 2 | 无 setCpuFixedPointMode | v2.cc:362 | 🟡 中 | 缺失功能 | ✅ 已添加 |
| 3 | SetInputBuffer 不是零拷贝 | v1/v2 | 🟢 低 | 设计问题（SDK 限制） | 已修正文档 |

### 6.2 与 SDK 一致性评估

| 维度 | 评估 | 说明 |
|------|------|------|
| ITensor 推理路径 | ✅ 一致 | 与 SDK 示例完全一致 |
| UserBuffer Float 路径 | ✅ 一致 | 与 SDK 示例完全一致 |
| UserBuffer TF8 路径 | ✅ 一致 | 与 SDK 示例一致，atlas 预提取 params 更优 |
| UserBuffer TF16 路径 | ❌ v1 有 Bug | v1 ParseDataType 映射错误 |
| 量化参数管理 | ✅ 更优 | atlas 预提取 + 统一采集优于 SDK v1 运行时 dynamic_cast |
| 输出格式控制 | ⭐ 更灵活 | manifest 支持 output dtype override，SDK 示例不支持 |
| 内存池复用 | ⭐ 新增 | SnpeMemoryPool 在 SDK 示例中不存在 |
| 共享缓冲 | ⭐ 新增 | shared_input 在 SDK 示例中不存在 |
| 构建选项 | ⚠️ 缺失一项 | v2 缺少 setCpuFixedPointMode |

### 6.3 整体评价

atlas-runtime 的 SNPE 后端实现整体质量良好，核心推理路径与 SDK 示例保持一致。相比 SDK 示例，atlas 在以下方面有所增强：

1. **`BuildTensorInfos()` 统一信息源** — 消除了 SDK 示例中反复调用 `getInputOutputBufferAttributes()` 的冗余
2. **预提取量化参数** — 避免了 SDK v1 中运行时 `dynamic_cast` 修改 encoding 的脆弱路径
3. **`AlignedBuffer` 对齐管理** — DSP 128 字节对齐要求，SDK 示例未体现
4. **`SnpeMemoryPool` 共享缓冲** — 多模型场景的内存优化，SDK 示例未涉及
5. **manifest 驱动配置** — 通过 manifest 统一管理 runtime/use_buffer/量化适配/输出格式

主要问题为 **v1 TF16 映射 BUG**（修复工作量小，约 5 行代码）和 **v2 未暴露 setCpuFixedPointMode**（增加约 10 行代码）。