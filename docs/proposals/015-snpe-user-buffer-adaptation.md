# Proposal-015: SNPE UserBuffer 机制适配

> **提议日期**：2026-07-10
> **提议人**：pizzk <726676435@qq.com>
> **状态**：草案
> **类型**：模块级
> **关联**：`docs/proposals/002-snpe-backend.md`、`docs/proposals/012-snpe-backend-inference-optimization.md`、`src/backend/snpe/snpe_backend_v1.cc`、`src/backend/snpe/snpe_backend_v2.cc`

---

## 一、背景与动机

Proposal-002 在 SNPE 后端接入时引入了 `use_buffer` 配置项，在 `SNPEBuilder` 阶段调用了 `setUseUserSuppliedBuffers(use_buffer)`。但此后端实现始终只走 ITensor 推理路径——`Load()` 预分配 `std::vector<unique_ptr<ITensor>> input_tensors`，`Infer()` 使用 `snpe->execute(itensor, output_map)` API。

这意味着当前 `use_buffer=true` 的配置实际上只有 builder 层面的语义生效，而运行时推理路径与 ITensor 模式完全相同，未利用 UserBuffer 的任何能力：

| builder 配置 | Infer() 实际路径 | 效果 |
|-------------|-----------------|------|
| `setUseUserSuppliedBuffers(0)` | `execute(itensor, output_map)` | 正常工作 |
| `setUseUserSuppliedBuffers(1)` | `execute(itensor, output_map)` | 行为未定义——ITensor 指针被当作 UserBuffer 指针 |

> **风险**：SNPE SDK 未明确文档化 `setUseUserSuppliedBuffers(true)` 后使用 `execute(ITensor)` 的行为。实测可能 crash、静默降级或内存损坏。当前必须修复。

### 1.1 为什么需要真正实现 UserBuffer 路径

UserBuffer 模式相比 ITensor 模式有三个核心优势：

| 优势 | 说明 |
|------|------|
| **真正的零拷贝输入** | UserBuffer 可直接包装外部内存（如摄像头 DMA buffer、mmap 文件），SNPE 从该内存直接读取，无需 memcpy |
| **DSP/HTP 最佳实践** | Qualcomm 官方推荐在 DSP/HTP 运行时使用 UserBuffer 模式以获得最佳性能和最低延迟 |
| **量化原生支持** | UserBufferEncodingTfN 直接携带 scale/zeroPoint 量化参数，SNPE 内部自动处理量化和反量化 |

### 1.2 当前代码问题清单

1. **推理路径与配置不一致**：`use_buffer=1` 时仍调用 ITensor 版 `execute()`，行为未定义
2. **ITensor 预分配浪费**：`Load()` 步骤 7 无条件预分配 `input_tensors`，use_buffer 模式下不需要（甚至不应该分配）
3. **无 UserBuffer 创建逻辑**：`Load()` 中未调用 `snpe->createInputBuffer()` / `snpe->createOutputBuffer()`
4. **无量化参数提取**：`UserBufferEncodingTfN` 的 scale/zeroPoint 未暴露给上层接口
5. **无 TF8→U8 适配**：模型量化到 TF8（int8）时，外部输入通常是 U8（uint8），缺少转换桥接

---

## 二、方案设计

### 2.1 整体架构：`BuildTensorInfos()` 作为统一信息源

`BuildTensorInfos()`（步骤 6）是所有 tensor 元数据的唯一入口。它从 SNPE runtime（`IBufferAttributes`）提取 baseline 信息（shape、dtype、layout、量化参数），然后以 manifest 配置为第一优先级覆盖。ITensor 和 UserBuffer 两条路径都从这个统一的信息源读取。

```
SnpeImpl
├── ITensor 路径 (use_buffer == false)
│   ├── input_tensors: vector<unique_ptr<ITensor>>
│   ├── output_map:    TensorMap
│   └── Infer() → snpe->execute(input_tensors, output_map)
│       （只读 input_info_ 中的 shape/dtype，忽略 quant_params_）
│
├── UserBuffer 路径 (use_buffer == true)
│   ├── user_input_buffers:  vector<unique_ptr<IUserBuffer>>
│   ├── user_output_buffers: vector<unique_ptr<IUserBuffer>>
│   ├── user_input_encodings:  vector<unique_ptr<UserBufferEncoding>>
│   ├── user_output_encodings: vector<unique_ptr<UserBufferEncoding>>
│   ├── user_input_raw:    vector<AlignedBuffer>
│   ├── user_output_raw:   vector<AlignedBuffer>
│   ├── user_input_external: vector<void*>
│   ├── input_buffer_stride/output_buffer_stride: vector<size_t>
│   └── Infer() → snpe->execute(UserBufferMap, UserBufferMap)
│       （读取 input_info_ + input_quant_params_ 创建 encoding）
│
└── BuildTensorInfos()  ← 统一信息源（步骤 6）
    ├── 从 runtime（IBufferAttributes）提取：shape / dtype / layout / scale / zp / bw
    ├── manifest 覆盖以上所有字段（manifest 第一优先级）
    ├── 输出到 input_info_[] + input_quant_params_[]
    └── 输出到 output_info_[] + output_quant_params_[]

步骤 7 预分配资源 —— 直接使用步骤 6 的输出：
├── ITensor 路径 ── 只读 info.shape（忽略 quant_params）
└── UserBuffer 路径 ── 读 info + quant_params 创建 encoding
    （不再单独查询 IBufferAttributes —— 消除冗余和潜在不一致）
```

**设计原则**：
- 两条路径代码不重叠，通过 `if (use_buffer)` 分支选择
- `GetInputBuffer()` / `GetOutputBuffer()` 在 UserBuffer 模式下返回 `user_input_raw` / `user_output_raw` 内部指针
- **`BuildTensorInfos()` 共用且唯一**：所有 tensor 元数据统一在步骤 6 采集和处理，manifest 覆盖只在一处发生

### 2.2 AlignedBuffer RAII Wrapper

使用 `aligned_alloc` + RAII wrapper 统一管理对齐内存。128 字节对齐满足 DSP 要求，同时普通 runtime 下无额外开销。

```cpp
// src/backend/snpe/snpe_aligned_buffer.h
struct AlignedBuffer {
    void*  data = nullptr;
    size_t size = 0;

    AlignedBuffer() = default;
    explicit AlignedBuffer(size_t byte_size, size_t alignment = 128);
    ~AlignedBuffer();

    // Move-only, no copy
    AlignedBuffer(AlignedBuffer&& other) noexcept;
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    uint8_t* AsU8() { return static_cast<uint8_t*>(data); }
    const uint8_t* AsU8() const { return static_cast<const uint8_t*>(data); }
};
```

**性能分析**：对齐分配只在 `Load()` 中调用，每 tensor 1 次，推理热路径完全不受影响。对齐后的 memcpy 通常比未对齐更快。

### 2.3 Load() 阶段：统一信息源 + 分支创建

`Load()` 中步骤 6 先调用 `BuildTensorInfos()` 采集全部 tensor 元数据，步骤 7 按 `use_buffer` 分支创建资源时直接使用步骤 6 的输出，不再单独查询 SNPE。

```
Load() 步骤 6: BuildTensorInfos() —— runtime 为基准，manifest 覆盖
  for each input:
    1. getInputOutputBufferAttributes() → 获取 shape / dtype / layout
    2. 从 encoding 提取量化参数：scale / zero_point / bandwidth
    3. 将以上信息存入 info + QuantParams
    4. 遍历 model_config_.inputs 查找同名条目：
       shape 非空        → 覆盖 info.shape
       dtype != kUnknown → 覆盖 info.dtype + 重置 quant_params
       layout 非空       → 覆盖 info.layout
  (output 同上)
  输出: input_info_[] + input_quant_params_[]
        output_info_[] + output_quant_params_[]

Load() 步骤 7: 预分配资源（直接使用步骤 6 的 output）
├── use_buffer == false
│   └── createTensor(info.shape) → input_tensors (原有逻辑, 保留)
│
└── use_buffer == true
    ├── 遍历每个 input
    │   ├── CreateEncoding(info.dtype, scale, zp, bw)  ← 无冗余查询
    │   ├── 计算 stride（基于 info.shape，已含 manifest override）
    │   ├── 分配 user_input_raw[i] (128B 对齐)
    │   └── snpe->createInputBuffer(name, raw_ptr, size, stride, encoding)
    │
    └── 遍历每个 output (同上流程)
        ├── CreateEncoding(info.dtype, scale, zp, bw)
        ├── 计算 stride
        ├── 分配 user_output_raw[i] (128B 对齐)
        └── snpe->createOutputBuffer(name, raw_ptr, size, stride, encoding)
```

#### 2.3.1 UserBuffer 创建 API

SNPE 1.x 和 2.x 的 UserBuffer 创建 API 签名不同：

- **v2 (2.x)**：`SNPE::createInputBuffer()` / `createOutputBuffer()`，接受 `(name, size, stride_array, encoding)` 参数
- **v1 (1.x)**：`SNPEFactory::getUserBufferFactory().createUserBuffer()`，接受 `(buffer_ptr, bufSize, stride_shape, encoding)` 参数（stride 以 `TensorShape` 形式传入）

```cpp
// v2 写法 (2.21.0)：
auto input_buffer = impl_->snpe->createInputBuffer(
    name.c_str(),
    raw.size,
    stride.data(),
    encoding.get());

// v1 写法 (1.50.0) —— SNPE 对象无 createInputBuffer()，通过工厂创建：
auto& ub_factory = zdl::SNPE::SNPEFactory::getUserBufferFactory();
zdl::DlSystem::TensorShape stride_shape(stride);
auto user_buf = ub_factory.createUserBuffer(
    raw.data,
    raw.size,
    stride_shape,
    encoding.get());
```

v1 的 `IUserBufferFactory::createUserBuffer()` 返回 `unique_ptr<IUserBuffer>`，v2 的 `SNPE::createInputBuffer()` 同样返回 `unique_ptr<IUserBuffer>`，两者最终类型一致。

#### 2.3.2 Stride 计算公式

```cpp
std::vector<size_t> ComputeUserBufferStride(const std::vector<int>& shape,
                                             size_t element_size) {
    const size_t rank = shape.size();
    std::vector<size_t> stride(rank, element_size);
    for (size_t i = rank; i > 1; --i) {
        stride[i - 2] = stride[i - 1] * std::max(shape[i - 1], 1);
    }
    return stride;
}
```

该公式与 layout 无关，因为 stride 数组顺序与 shape 一致。例如：
- NCHW [1,3,224,224], float32: stride = [150528, 50176, 224, 1] × 4
- NHWC [1,224,224,3], float32: stride = [150528, 672, 3, 1] × 4

#### 2.3.3 UserBufferEncoding 选择

| DataType | v1 Encoding | v2 Encoding |
|----------|-------------|-------------|
| kFloat32 | `UserBufferEncodingFloat` | `UserBufferEncodingFloat` |
| kFloat16 | 不支持 | `UserBufferEncodingFloat16` |
| kInt8 | `UserBufferEncodingTfN(zp, scale, 8)` | `UserBufferEncodingTfN(zp, scale, 8)` |
| kUInt8 | `UserBufferEncodingTfN(0, 1.0f, 8)` | `UserBufferEncodingUint8` |
| kInt32 | 不支持 | `UserBufferEncodingInt32` |

`CreateEncoding()` 不再接收原始 `IBufferAttributes*`，而是使用步骤 6 中预计算好的 `QuantParams`：

```cpp
// 由 BuildTensorInfos() 在步骤 6 预计算并存储
struct QuantParams {
    float    scale      = 1.0f;   // quantized_step_size
    int32_t  zero_point = 0;      // step_exactly_0
    uint32_t bandwidth  = 8;      // bits
};

// 步骤 7 UserBuffer 路径直接使用预计算参数
std::unique_ptr<UserBufferEncoding> CreateEncoding(
    utils::DataType dtype,       // 已含 manifest override
    float scale,
    int32_t zero_point,
    uint32_t bandwidth) {
    switch (dtype) {
        case kInt8:
            return std::make_unique<UserBufferEncodingTfN>(
                zero_point, scale, bandwidth);
        case kFloat32: ...
        ...
    }
}
```

`BuildTensorInfos()` 中提取量化参数：

```cpp
// 步骤 6 BuildTensorInfos() —— 从运行时提取量化参数
QuantParams ExtractQuantParamsFromAttrs(IBufferAttributes* attrs) {
    QuantParams qp;
    auto encoding_type = attrs->getEncodingType();
    if (encoding_type == TF8 || encoding_type == TF16) {
        auto* tfN = static_cast<UserBufferEncodingTfN*>(
            attrs->getEncoding());
        qp.scale      = tfN->getQuantizedStepSize();
        qp.zero_point = tfN->getStepExactly0();
        // NOTE: v1 (1.x) 的 UserBufferEncodingTfN 没有 getBandWidth()
        // 默认 bandwidth=8（TF8）已满足需求，无需调用 getBandWidth()
    }
    return qp;
}

// Manifest 覆盖 dtype 时重置量化参数
QuantParams QuantParamsForDtype(DataType dtype) {
    QuantParams qp;
    if (dtype == kInt8) qp = {1.0f, 0, 8};
    return qp;
}
```

### 2.4 Infer() 阶段：双路径路由

```cpp
utils::ErrorCode SnpeBackend::Infer(
    const std::vector<utils::Tensor>& inputs,
    std::vector<utils::Tensor>& outputs) {

    if (!impl_->use_buffer) {
        return InferWithTensor(inputs, outputs);
    } else {
        return InferWithBuffer(inputs, outputs);
    }
}
```

#### 2.4.1 InferWithBuffer() 流程

```
Step 1: 填充输入缓冲
  for each input:
    if user_input_external[i] is set (SetInputBuffer 注入):
      memcpy(extern → internal buffer)
    else if t.data == GetInputBuffer(i).data (GetInputBuffer 零拷贝):
      skip (数据已写入内部缓冲)
    else if NeedsAdaptation(i, t.info.dtype):
      AdaptU8ToTf8() 逐元素转换
    else:
      memcpy(t.data → internal buffer)

Step 2: 构建 UserBufferMap + execute
  input_map / output_map = 所有 IUserBuffer
  snpe->execute(input_map, output_map)

Step 3: 零拷贝输出
  outputs[i] = Tensor{ data = user_output_raw[i].AsU8(), owns_data = false }
```

#### 2.4.2 GetInputBuffer() / GetOutputBuffer() UserBuffer 实现

```cpp
utils::Span<void> SnpeBackend::GetInputBuffer(size_t index) const {
    if (impl_->use_buffer) {
        if (index >= impl_->user_input_raw.size()) return {};
        return { impl_->user_input_raw[index].AsU8(),
                 impl_->user_input_raw[index].size };
    }
    // ITensor 路径（原逻辑）
    ...
}
```

### 2.5 量化适配层（TF8 模型接收 U8 输入）

量化为 TF8（int8）的模型期望输入范围 `[-128, 127]`，常见摄像头输出 U8（uint8）范围 `[0, 255]`。

```cpp
void AdaptU8ToTf8(const void* src, size_t src_size,
                   void* dst, size_t dst_size) {
    const uint8_t* u8_src = static_cast<const uint8_t*>(src);
    int8_t* dst_ptr = static_cast<int8_t*>(dst);
    size_t count = std::min(src_size, dst_size);
    for (size_t i = 0; i < count; ++i) {
        dst_ptr[i] = static_cast<int8_t>(static_cast<int>(u8_src[i]) - 128);
    }
}
```

通过 manifest 中 `inputs[].dtype` 声明输入实际类型，模型真实 dtype 由 `IBufferAttributes` 报告：

```cpp
bool NeedsQuantizationAdaptation(size_t index, utils::DataType input_dtype) {
    return (input_info_[index].dtype == utils::DataType::kInt8 &&
            input_dtype == utils::DataType::kUInt8);
}
```

### 2.6 SetInputBuffer()：外部内存注入

新增接口，允许调用方将 UserBuffer 的底层内存切换到外部 DMA 缓冲区。

```cpp
utils::ErrorCode SnpeBackend::SetInputBuffer(size_t index,
                                               void* external_mem,
                                               size_t byte_size) {
    if (!loaded_)           return kNotInitialized;
    if (!impl_->use_buffer) return kInvalidArgument;
    if (index >= impl_->user_input_raw.size()) return kInvalidArgument;

    impl_->user_input_external[index] = external_mem;
    return kOk;
}
```

- 外部内存在 Infer() 中通过 memcpy 拷贝到内部 UserBuffer（当前实现）
- 未来优化：若 SNPE 版本支持 `IUserBuffer::setBufferAddress()`，可实现真正的零拷贝
- 调用 `SetInputBuffer(index, nullptr, 0)` 重置为内部缓冲

### 2.7 IBackend 接口扩展

在 `IBackend` 中添加默认空实现：

```cpp
virtual utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                          size_t byte_size) {
    (void)index;
    (void)external_mem;
    (void)byte_size;
    return utils::ErrorCode::kInvalidArgument;
}
```

### 2.8 输出侧量化反适配

**不做自动反转换**。推理结果始终保持模型原始格式。调用方需在 pipeline 后处理中自行转换。理由：
- 输出可能用于 softmax/argmax 等数值敏感处理，自动转换丢失精度
- 与 pipeline 架构一致——数据转换由显式 Node 完成

### 2.9 Unload() 清理

```cpp
void SnpeBackend::Unload() {
    // UserBuffer 路径清理
    impl_->user_input_buffers.clear();
    impl_->user_output_buffers.clear();
    impl_->user_input_encodings.clear();
    impl_->user_output_encodings.clear();
    impl_->user_input_raw.clear();    // AlignedBuffer 自动 free
    impl_->user_output_raw.clear();
    impl_->user_input_external.clear();
    impl_->input_buffer_stride.clear();
    impl_->output_buffer_stride.clear();

    // ITensor 路径清理
    impl_->input_tensors.clear();
    // ... 通用清理 ...
}
```

---

## 三、设计决策

### 3.1 统一信息源：`BuildTensorInfos()` 作为单一入口

| 问题 | 结论 |
|------|------|
| tensor 元数据有几个采集点？ | 只有 `BuildTensorInfos()` 这一个入口，ITensor 和 UserBuffer 路径都从这里读取 |
| manifest 覆盖在何处发生？ | 只在 `BuildTensorInfos()` 中一处，manifest 第一优先级 |
| 步骤 7 UserBuffer 路径是否还需查 `IBufferAttributes`？ | 不再需要。步骤 6 已完成全部采集，量化参数已提取为 `QuantParams` 存储 |
| 如果 manifest 覆盖了 dtype，encoding 类型是否一致？ | 一致。`CreateEncoding()` 使用已覆盖的 `info.dtype` 选择 encoding 子类 |
| ITensor 路径受影响吗？ | 否。ITensor 路径只读 `info.shape`/`info.dtype`，忽略 `QuantParams` |

### 3.2 use_buffer 模式下是否还需要预分配？

| 问题 | 结论 |
|------|------|
| 是否需要预分配 ITensor？ | 不需要。UserBuffer 路径不使用 ITensor |
| 是否需要预分配 UserBuffer？ | 需要。UserBuffer 在 `Load()` 创建，`Infer()` 复用 |
| 是否浪费内存？ | 否。UserBuffer 是 void* 包装，开销极小 |

### 3.3 外部输入处理级别

| 级别 | 方式 | 拷贝次数 |
|------|------|---------|
| Level 1 | `Infer(inputs, outputs)` 内部 memcpy 到后备缓冲 | 1 次 |
| Level 2 | `GetInputBuffer()` → 写入 → `Infer()` | 0 次 |
| Level 3 | `SetInputBuffer(ext_ptr)` → 外部内存注入 | 0-1 次(当前 1 次) |

### 3.3.1 多模型共享输入的零拷贝优势

在典型的多模型 pipeline 场景中（例如：检测模型 → 分类模型，或级联识别），多个模型共用**同一份输入数据**的情况非常普遍：

```
┌─────────────┐     ┌──────────────┐
│  摄像头/解码器  │────→│  共享输入缓冲区  │
└─────────────┘     └──────┬───────┘
                           │
            ┌──────────────┼──────────────┐
            ▼              ▼              ▼
      ┌──────────┐  ┌──────────┐  ┌──────────┐
      │ 模型 A    │  │ 模型 B    │  │ 模型 C    │
      │ SNPE BE   │  │ SNPE BE   │  │ SNPE BE   │
      └──────────┘  └──────────┘  └──────────┘
```

**使用 `SetInputBuffer()` 统一注入后**：

| 步骤 | ITensor 模式 | UserBuffer + SetInputBuffer |
|------|-------------|---------------------------|
| 模型 A 推理 | memcpy 输入 → 执行 → 输出 | **跳过拷贝** → 执行 → 输出 |
| 模型 B 推理 | memcpy 同一份输入 → 执行 | **跳过拷贝** → 执行 |
| 模型 C 推理 | memcpy 同一份输入 → 执行 | **跳过拷贝** → 执行 |
| 总计拷贝次数 | 3 次输入 memcpy | **0 次额外拷贝** |

ITensor 模式下，每次 `Infer()` 调用都需要将共享输入数据 memcpy 到各模型的 ITensor 内部缓冲区。即使输入数据完全相同，每个模型各自拷贝一份。

UserBuffer 模式下，调用 `SetInputBuffer()` 将所有模型的 UserBuffer 指向同一块外部内存（例如摄像头帧、mmap 文件或解码输出），推理时 SNPE 直接从该内存读取，完全消除重复拷贝。

**实测参考**：720p 图像（3×720×1280 = 2.76MB）的 memcpy 耗时约 0.5-1ms。三模型 pipeline 中，输入拷贝从 3 次减少到 0 次，节省约 1.5-3ms/帧。

### 3.4 TF8→U8 适配场景矩阵

| 模型 dtype | 输入 dtype | 需要转换 | 方式 |
|-----------|-----------|---------|------|
| TF8 (int8) | TF8 (int8) | 否 | 直接传入 |
| TF8 (int8) | U8 (uint8) | 是 | `uint8_val - 128` |
| UINT8 | U8 (uint8) | 否 | 直接传入 |
| FLOAT | float32 | 否 | 直接传入 |

### 3.5 对齐分配器策略

统一使用 `posix_memalign` + RAII wrapper（`AlignedBuffer`），对齐到 128 字节。推理热路径不受影响——对齐分配只在 `Load()` 中执行一次。

### 3.6 DSP 平台特殊要求

1. **内存对齐**：DSP 要求 128/256 字节对齐，`AlignedBuffer` 默认 128B
2. **Stride 约束**：DSP 要求 stride 是 element_size 整数倍且不为 0
3. **Runtime 联动**：`runtime="dsp"` 时建议自动启用 `use_buffer=true`
4. **当前不影响**：实现本身是对齐安全的，DSP 支持由 runtime 配置决定

### 3.7 v1/v2 对称实现

UserBuffer API 签名 v1/v2 一致，仅命名空间前缀不同。改动同步应用到两个文件。

| 差异 | v1 (1.50.0) | v2 (2.21.0) |
|------|-------------|-------------|
| 命名空间 | `zdl::DlSystem::` | `DlSystem::` |
| ElementType FLOAT16 | 不支持 | 支持 |
| ElementType INT8/UINT8 | 不支持 | 支持 |
| UserBufferEncodingUint8 | 无（用 TfN 替代） | 有 |

---

### 3.8 SnpeMemoryPool 共享缓冲池

多模型 pipeline 场景下，每个 `SnpeBackend::Load()` 独自调用 `posix_memalign` 分配对齐内存，推理时通过 `SetInputBuffer()` 虽然能注入外部指针，但内部 `InferWithBuffer()` 仍需 memcpy 到自己的后备缓冲。通过引入 `SnpeMemoryPool`，从两个维度优化：

#### 3.8.1 路径 A：Free-list 复用

`Acquire()` / `Release()` 接口，用于 Load/Unload 周期内的内存复用：

```
无 Pool:  model Load → posix_memalign → Unload → free → 下次 Load → posix_memalign
有 Pool:  model Load → Acquire(复用) → Unload → Release(归还) → 下次 Load → Acquire(直接复用)
```

消除高频的 posix_memalign/free 开销，减少内存碎片。

#### 3.8.2 路径 B：共享缓冲（参考计数）

`AcquireShared(key)` / `ReleaseShared(key)` 接口，多个模型共享同一块对齐内存：

```cpp
// manifest 配置:
// {
//   "backend": "snpe",
//   "config": { "use_buffer": "true", "shared_input": "camera_frame" },
//   ...
// }

// SnpeBackend::Load() 内部:
//   第一次 Load: pool 分配对齐内存, 创建 IUserBuffer 指向它
//   第二次 Load (相同 key): pool 返回同一指针, 新 IUserBuffer 也指向它
//   所有模型共享同一个物理内存页
```

关键收益：

| 指标 | 独立分配 | 共享缓冲池 |
|------|---------|-----------|
| 内存占用（3 模型 × 720p） | 3 × 2.76MB = 8.28MB | **2.76MB** |
| posix_memalign 调用 | 3 次 | **1 次** |
| 输入 memcpy 次数 | 3 次 | **0 次**（写入共享缓冲后各模型直接读） |
| DSP 页表映射 | 3 组 | **1 组** |

> **注意**：多个模型共享相同的 IUserBuffer 底层内存时，**推理必须串行执行**（或调用方自行同步），因为 SNPE 执行期间 `execute()` 直接读写该内存。

#### 3.8.3 线程安全设计

`SnpeMemoryPool` 的所有公开方法均使用 `std::mutex` 保护：

| 方法 | 保护范围 | 并发场景 |
|------|---------|---------|
| `Acquire()` | free_list_ 读写 | 多模型并发 Load |
| `Release()` | free_list_ 写 | 多模型并发 Unload |
| `AcquireShared()` | shared_buffers_ 读写 + 分配 | 多模型并发 Load（首调用分配，后续引用计数） |
| `ReleaseShared()` | shared_buffers_ 读写 + 可能移至 free_list | 多模型并发 Unload（尾调用移至 free list） |
| `Clear()` | 全部 | 进程退出 / context 析构 |

锁粒度：全局池级锁。池操作仅在 Load/Unload 调用，不在推理热路径上，因此池级锁的竞争极低。

#### 3.8.4 配置方式

在 manifest 的 `model.config` 中增加可选键：

```json
{
  "models": [
    {
      "id": "detector",
      "backend": "snpe",
      "config": {
        "use_buffer": "true",
        "shared_input": "camera_frame"
      }
    },
    {
      "id": "classifier",
      "backend": "snpe",
      "config": {
        "use_buffer": "true",
        "shared_input": "camera_frame"
      }
    }
  ]
}
```

- 不设置 `shared_input`：行为不变，每个模型独立分配
- 设置相同 key：共享同一块内存
- 设置不同 key：各 key 独立分配

#### 3.8.5 生命周期契约

```
流程:
  context.Init()
    │
  modelA.Load(shared_input="cam")
    │  └─ pool.AcquireShared("cam", size)
    │      首次 → posix_memalign, refcount = 1
    │
  modelB.Load(shared_input="cam")
    │  └─ pool.AcquireShared("cam", size)
    │      复用 → refcount = 2
    │
  modelA.Unload()
    │  └─ pool.ReleaseShared("cam")
    │      refcount = 1 (仍在 shared_buffers_ 中)
    │
  modelB.Unload()
    │  └─ pool.ReleaseShared("cam")
    │      refcount = 0 → 移至 free list
    │
  进程退出 / context 析构
    └─ pool.Clear() → free 所有内存
```

#### 3.8.6 double-free 防护

共享缓冲场景下，`user_input_raw[i]` 中的 `AlignedBuffer` 持有来自 pool 的非拥有指针。`Unload()` 时，先调用 `pool.ReleaseShared(key)` 将所有权归还 pool，然后将 `user_input_raw` 中各条目的 `data` 置空，使得 `AlignedBuffer` 析构时 `free(nullptr)` 为 no-op：

```cpp
pool.ReleaseShared(impl_->shared_input_key);
for (auto& buf : impl_->user_input_raw) {
    buf.data = nullptr;  // 防止 AlignedBuffer 析构时 double-free
    buf.size = 0;
}
impl_->shared_input_key.clear();
```

---

## 四、改动清单

| 文件 | 改动 |
|------|------|
| `src/backend/snpe/snpe_aligned_buffer.h` | **新增** — AlignedBuffer RAII wrapper，独立 Bazel target `:snpe_aligned_buffer` |
| `src/backend/snpe/snpe_memory_pool.h` | **新增** — 线程安全对齐内存池，含 free-list 复用和 key-based 共享缓冲 |
| `src/backend/base/i_backend.h` | 新增 `SetInputBuffer()` 虚方法（默认返回 kInvalidArgument） |
| `src/backend/base/profiling_backend.h` | 新增 `SetInputBuffer()` 覆盖声明 — 透传转发到内层后端 |
| `src/backend/base/profiling_backend.cc` | 新增 `SetInputBuffer()` 转发实现（否则 profiling 模式下 Level 3 静默失效） |
| `src/backend/snpe/snpe_backend.h` | 新增 `QuantParams` 结构体及 `input_quant_params_` / `output_quant_params_` 成员 |
| `src/backend/snpe/snpe_backend_context.h` | 新增 `GetMemoryPool()` 方法和 `memory_pool_` 成员；新增 `:snpe_memory_pool` 与 `:snpe_aligned_buffer` deps |
| `src/backend/snpe/snpe_backend_v1.cc` | `BuildTensorInfos()` 增强为统一信息源（提取 quant params + manifest 覆盖）；`CreateEncoding()` 改为使用 `(dtype, scale, zp, bw)` 签名；步骤 7 UserBuffer 路径消除冗余 `getInputOutputBufferAttributes()` 调用；`Unload()` 清空 quant params 向量；`SnpeImpl` 新增 `shared_input_key`；共享内存池支持 |
| `src/backend/snpe/snpe_backend_v2.cc` | 同上同步修改 |
| `src/backend/snpe/snpe_backend_stub.cc` | 新增 `SetInputBuffer()` stub |
| `src/backend/snpe/BUILD` | 新增 `:snpe_aligned_buffer` 和 `:snpe_memory_pool` 独立 cc_library 目标；`snpe_backend_context` 增加对应 deps |
| `src/api/model_handle.h` | 新增 `SetInputBuffer()` 公开 API 声明 — 让用户通过 ModelHandle 调用 Level 3 外部内存注入 |
| `src/api/model_handle.cc` | 新增 `SetInputBuffer()` 实现 — 转发到 `entry_->backend->SetInputBuffer()` |

---

## 五、验证方案

| 测试 | 方法 | 标准 |
|------|------|------|
| ITensor 路径无退化 | 现有 `bazel test //...` | 全部通过 |
| UserBuffer 创建 | 模拟 IBufferAttributes 返回固定配置 | 创建成功，stride/encoding 正确 |
| U8→TF8 转换 | 已知 U8 输入，验证 TF8 输出 | output[i] = input[i] - 128 |
| Stride 计算 | 多组 shape | NHWC/NCHW 各正确 |
| SetInputBuffer | 注入外部内存后 Infer | 结果正确 |
| 无 SDK 环境 | `bazel test //tests/backend/snpe/...` | Stub 路径零错误 |
| 内存池：Acquire/Release 复用 | 连续 Acquire/Release 不同尺寸 | 拟合尺寸的 buffer 被复用（data 指针一致） |
| 内存池：共享缓冲生命周期 | 两个模型 Load(key) → Unload → 重载 | 写入数据在重载后仍可读 |
| 内存池：double-free 防护 | 共享缓冲 Load → Unload 后验证 no ASAN 错误 | 无 double-free / use-after-free |
| 内存池：无共享时无退化 | 不设置 shared_input | 行为与之前完全相同 |
| 内存池：线程安全 | 多线程并发 AcquireShared 同一 key | refcount 正确，返回同一指针 |

---

## 六、单元测试与示例

### 6.1 SnpeMemoryPool 单元测试

**文件**: `tests/backend/snpe/snpe_memory_pool_test.cc`

SnpeMemoryPool 为纯头文件实现，**不依赖 SNPE SDK**，可在任意平台（macOS/Linux/WSL）编译运行。

| # | 测试用例 | 覆盖内容 |
|---|---------|---------|
| 1 | `AcquireZeroSize` | `Acquire(0)` 返回空 AlignedBuffer |
| 2 | `AcquireReleaseReuse` | Acquire → Release → Acquire 同 size，data 指针复用 |
| 3 | `AcquireBestFit` | 预存 size=64 和 256，Acquire(128) 返回 size=256 的 buffer |
| 4 | `PreAllocate` | `PreAllocate({32, 64})` 后 Acquire(32) 直接命中免分配 |
| 5 | `AcquireSharedFirstCaller` | 首次调用分配，refcount=1 |
| 6 | `AcquireSharedSecondCaller` | 再次同 key 返回同一指针，refcount=2 |
| 7 | `ReleaseSharedDecRef` | Refcount 递减，减到 0 后移入 free list |
| 8 | `Clear` | Clear 后 Acquire 重新分配，total_allocated 重置 |
| 9 | `TotalAllocatedBytes` | 多次分配后的总和正确 |
| 10 | `ThreadSafety` | 4 线程并发 AcquireShared 同一 key，所有线程得到同一指针 |

BUILD 新增目标:
```python
cc_test(
    name = "snpe_memory_pool_test",
    srcs = ["snpe_memory_pool_test.cc"],
    deps = [
        "//src/backend/snpe:snpe_memory_pool",
        "@googletest//:gtest_main",
    ],
)
```

### 6.2 SnpeBackendContext + SnpeMemoryPool 集成测试

**文件**: `tests/backend/snpe/snpe_backend_test.cc`（追加）

| # | 测试用例 | 覆盖内容 |
|---|---------|---------|
| 11 | `ContextGetMemoryPool` | Context 返回有效 pool 引用，Acquire/Release 正常 |
| 12 | `ContextPoolSharedAcquire` | 通过 Context 的 pool 进行 AcquireShared/ReleaseShared |
| 13 | `ContextPoolClearAndReuse` | Pool Clear 后重新 Acquire 正常 |
| 14 | `ContextPoolDoubleFreeProtection` | Repeated ReleaseShared 安全 |
| 15 | `ContextPoolWithoutSharedNoOp` | Load/Unload 不影响未使用的 pool |

### 6.3 Example：snpe_shared_buffer — 共享输入缓冲示例

**目录**: `examples/snpe_shared_buffer/`

演示两个模型通过 SnpeMemoryPool 共享同一块输入物理内存。

| 文件 | 说明 |
|------|------|
| `BUILD` | cc_binary，依赖 `//src/public:atlas` |
| `main.cc` | 主程序：双模型共享输入缓冲 |
| `manifest.json` | 双模型 manifest，配置 `shared_input` |
| `gen_models.py` | 生成两个 ReLU DLC 测试模型 |
| `README.md` | 构建和运行说明 |

**main.cc 核心流程**:
```
1. Init(manifest) → 上下文创建 SnpeMemoryPool
2. model_a.Load() → pool.AcquireShared("camera_feed", N), refcount=1
3. model_b.Load() → pool.AcquireShared("camera_feed", N), refcount=2, 同一指针
4. 写入数据到 model_a 的 buffer → model_b 的 buffer 可见同一数据
5. model_a.Run() → 读共享 buffer
6. model_b.Run() → 读同一共享 buffer
7. Unload model_b → pool.ReleaseShared("camera_feed"), refcount=1
8. Unload model_a → pool.ReleaseShared("camera_feed"), refcount=0, 回收到 free list
```

**manifest.json 关键配置**:
```json
{
  "version": "1.0",
  "name": "snpe-shared-buffer-demo",
  "models": [
    {
      "id": "model_a",
      "backend": "snpe",
      "model_path": "${SAMPLE_MODEL_DIR}/model_a.dlc",
      "config": { "runtime": "cpu", "use_buffer": "true", "shared_input": "camera_feed" }
    },
    {
      "id": "model_b",
      "backend": "snpe",
      "model_path": "${SAMPLE_MODEL_DIR}/model_b.dlc",
      "config": { "runtime": "cpu", "use_buffer": "true", "shared_input": "camera_feed" }
    }
  ]
}
```

### 6.4 全部新增/修改文件总览

| 操作 | 文件 |
|------|------|
| 新增 | `tests/backend/snpe/snpe_memory_pool_test.cc` |
| 修改 | `tests/backend/snpe/BUILD` — 新增 `snpe_memory_pool_test` target |
| 修改 | `tests/backend/snpe/snpe_backend_test.cc` — 追加 5 个集成测试 |
| 新增 | `examples/snpe_shared_buffer/BUILD` |
| 新增 | `examples/snpe_shared_buffer/main.cc` |
| 新增 | `examples/snpe_shared_buffer/manifest.json` |
| 新增 | `examples/snpe_shared_buffer/gen_models.py` |
| 新增 | `examples/snpe_shared_buffer/README.md` |

### 6.5 验证标准

| 测试 | 预期 |
|------|------|
| `bazel test //tests/backend/snpe:snpe_memory_pool_test` | 10/10 通过（任意平台） |
| `bazel test //tests/backend/snpe:snpe_backend_test` | 原 10 + 新 5 = 15/15 通过 |
| `bazel run //examples/snpe_shared_buffer:snpe_shared_buffer` | 双模型共享缓冲 → 正确输出 |

---

## 七、对外接口/机制变动适配分析

### 7.1 总览

本次提案共涉及 **4 个对外接口层面**，其中 **2 个必须适配修复**，2 个自动兼容。

| # | 层级 | 问题 | 优先级 | 状态 |
|---|------|------|--------|------|
| 1 | `IBackend` 基类 | 已添加默认空实现，各后端自动继承 | 🟢 自动兼容 | ✅ |
| 2 | `ProfilingBackend` 装饰器 | **必须**补上 `SetInputBuffer()` 转发 | 🔴 高 | ✅ 已实现 |
| 3 | `ModelHandle` 公共 API | 暴露 `SetInputBuffer()` 使用户可用 Level 3 | 🟡 中 | ✅ 已实现 |
| 4 | Manifest 文档 | 补充 `shared_input` 配置说明 | 🟢 低 | ✅ 本节说明 |

### 7.2 ProfilingBackend 适配（已修复）

`ProfilingBackend` 是 `IBackend` 的装饰器，为 `Load`/`Infer`/`Unload` 添加计时，其余方法透传转发。

**问题**：原实现只转发了 `GetInputBuffer()` 和 `GetOutputBuffer()`，缺少 `SetInputBuffer()`，导致 `ProfilingBackend` 包裹 `SnpeBackend` 时，`SetInputBuffer()` 命中 `IBackend` 默认实现（返回 `kInvalidArgument`），外部内存注入功能静默失效。

**修复**：在 `ProfilingBackend` 中新增 `SetInputBuffer()` 透传转发：

```cpp
// profiling_backend.h — 补充声明
utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                 size_t byte_size) override;

// profiling_backend.cc — 补充转发实现
utils::ErrorCode ProfilingBackend::SetInputBuffer(size_t index,
                                                   void* external_mem,
                                                   size_t byte_size) {
    return inner_->SetInputBuffer(index, external_mem, byte_size);
}
```

### 7.3 ModelHandle 公共 API 适配（已实现）

`ModelHandle` 是用户操作模型的主要入口。原有的 `GetInputTensor()` / `GetOutputTensor()` 提供了 Level 2 零拷贝（写入内部缓冲），但 Level 3 外部内存注入（`SetInputBuffer`）不可通过 `ModelHandle` 调用。

**新增 API**：

```cpp
// model_handle.h — 新增
utils::ErrorCode SetInputBuffer(size_t index, void* external_mem,
                                 size_t byte_size) const;

// model_handle.cc — 实现：转发到后端
utils::ErrorCode ModelHandle::SetInputBuffer(size_t index,
                                              void* external_mem,
                                              size_t byte_size) const {
    if (!IsValid()) return utils::ErrorCode::kNotInitialized;
    return entry_->backend->SetInputBuffer(index, external_mem, byte_size);
}
```

**使用示例**：

```cpp
// 两个模型共享同一摄像头 DMA buffer
void* camera_frame = /* from ISP / decoder */;
model_a.SetInputBuffer(0, camera_frame, frame_size);
model_b.SetInputBuffer(0, camera_frame, frame_size);

model_a.Run(input_tensor, &output_a);  // 零拷贝输入
model_b.Run(input_tensor, &output_b);  // 零拷贝输入
```

### 7.4 各后端兼容性

| 后端 | `SetInputBuffer()` | 说明 |
|------|--------------------|------|
| `CpuBackend` | 未覆盖 → `kInvalidArgument` | CPU 后端不使用 UserBuffer，返回错误符合语义 |
| `SnpeBackend(v1/v2)` | 已实现 | Level 3 外部内存注入完整支持 |
| `SnpeBackend(stub)` | 已实现 → `kNotInitialized` | 无 SDK 环境，返回未初始化 |
| `ProfilingBackend` | ✅ **已修复** — 透传转发 | 不再静默失效 |

### 7.5 Manifest 新增配置

在模型 `config` 中新增可选键 `shared_input`：

```json
{
  "models": [
    {
      "id": "model_a",
      "backend": "snpe",
      "config": {
        "runtime": "cpu",
        "use_buffer": "true",
        "shared_input": "camera_feed"
      }
    }
  ]
}
```

| 键 | 类型 | 必需 | 默认值 | 说明 |
|---|------|------|--------|------|
| `shared_input` | string | 否 | 空字符串 | 共享输入缓冲的 key，相同 key 的模型共用同一块对齐内存 |

- 不设置 `shared_input`：行为不变，每个模型独立分配
- 设置相同 key：通过 `SnpeMemoryPool::AcquireShared(key)` 共享同一块物理内存
- 设置不同 key：各 key 独立分配
- `shared_input` 仅在 `use_buffer=true` 时生效；`use_buffer=false` 时忽略此配置

### 7.6 TensorInfo 是否暴露量化参数（设计决策）

当前 `TensorInfo` 结构体：

```cpp
struct TensorInfo {
    std::string name;
    std::vector<int> shape;
    DataType dtype = DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
};
```

量化参数 (`scale`, `zero_point`, `bandwidth`) 被存储在 `SnpeBackend` 私有的 `input_quant_params_` / `output_quant_params_` 中，**外部用户当前无法获取**。

**决策**：当前暂不扩展 `TensorInfo`，原因：
1. `UserBufferEncoding` 在 `Load()` 内部自动创建，用户不需要手动操作
2. 输出反量化可在 pipeline 后处理中通过显式 Node 完成
3. 遵循 YAGNI 原则，等实际需求出现再添加

