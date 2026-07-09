# Proposal-012: SNPE 后端推理性能优化

> **提议日期**：2026-07-09
> **完成日期**：2026-07-09
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已完成
> **类型**：模块级
> **关联**：`docs/proposals/002-snpe-backend.md`（SNPE 后端基础）、`src/backend/snpe/snpe_backend_v1.cc`、`src/backend/snpe/snpe_backend_v2.cc`

---

## 一、背景与动机

Proposal-002 实现了 SNPE 后端的基本接入，提供了 `SnpeBackend::Infer()` 接口。与外部参考实现（`insightface/snpe_cpp/faceid_internal.cpp`）对比实测发现，Atlas SNPE 后端的单次推理耗时明显较高。

### 1.1 性能差异来源

通过对两种实现逐行对比，定位到三个主要性能差距：

| 问题 | 外部参考实现 | Atlas 当前实现 | 影响程度 |
|------|-------------|---------------|---------|
| ITensor 生命周期 | 构造时创建，多次推理复用（只 `memcpy`） | 每次 `Infer()` 重新 `createTensor()` | **高** |
| 输出数据处理 | 直接返回 SNPE tensor 内部指针，零拷贝 | `malloc` + `memcpy` 到独立缓冲区 | **中** |
| 输入数据拷贝 | 外部写入 ITensor 内部缓冲区后零拷贝推理 | `Infer()` 内 `std::copy`/`memcpy` 到 ITensor | **中** |
| 默认 Runtime | `DSP` + `HIGH_PERFORMANCE` | `GPU` + `BALANCED` | **中** |

### 1.2 额外隐患

输出 `byte_size` 计算逻辑使用了 manifest 覆盖后的 `info.dtype`，可能与 SNPE 实际输出 dtype 不一致，导致 `memcpy` 溢出或读不全。**注意**：此问题由使用者通过 manifest 保证 dtype 一致性来解决，本次优化不涉及代码改动（改为零拷贝后此问题已自然消除）。

---

## 二、优化方案

### 2.1 输入 ITensor 复用（核心优化）

**当前流程**（每次 `Infer()`）：
```
TensorShape(_shape)
    → tensor_factory::createTensor()       ← 分配新 ITensor
    → std::copy(input_data, itensor->begin())  ← 拷贝数据
    → execute(itensor, output_map)
    → itensor 析构                           ← 释放
```

**优化后流程**（`Load()` 时预分配）：
```
// Load() 阶段
createTensor(tensor_shape) → 存入 impl_->input_tensors[]

// 每次 Infer()
memcpy(impl_->input_tensors[i]->begin(), input_data, byte_size)  ← 仅拷贝
execute(impl_->input_tensors[i].get(), output_map)
```

**改动范围**：

- `SnpeImpl`：新增 `std::vector<std::unique_ptr<zdl::DlSystem::ITensor>> input_tensors`
- `Load()`：步骤 7 中创建 ITensor 并缓存
- `Infer()`：只用 `std::copy`/`memcpy` 填充数据，不再创建 ITensor
- `Unload()`：清理 `input_tensors`

### 2.2 输出零拷贝（消除 memcpy 开销）

将输出的 `TensorMap` 作为 `SnpeImpl` 成员变量（`output_map`），`Infer()` 直接获取 SNPE 内部数据指针，`owns_data = false` 标记为借用指针，避免每次推理的 `malloc` + `memcpy` 开销。

```cpp
// SnpeImpl 中缓存输出 TensorMap
zdl::DlSystem::TensorMap output_map;

// Infer() 中零拷贝
zdl::DlSystem::ITensor* out_itensor =
    impl_->output_map.getTensor(impl_->output_names[i].c_str());
auto raw_ptr = out_itensor->cbegin();
utils::Tensor out;
out.data      = const_cast<void*>(static_cast<const void*>(&(*raw_ptr)));
out.byte_size = out_itensor->getSize() * ElementByteSize(info.dtype);
out.owns_data = false;  // 调用方必须在下次 Infer() 前消费完毕
```

**注意事项**：
- 返回的指针生命周期与 `output_map` 绑定，下次 `Infer()` 调用会覆盖
- 调用方如需延长生命周期，需自行拷贝

### 2.3 默认 Runtime 与 Performance Profile

- 默认 `runtime` 改为通用 `GPU`（`Runtime_t::GPU`），不再使用 `GPU_FLOAT32_16_HYBRID` 等具体精度变体。`ParseRuntime()` 支持 `CPU`/`GPU`/`DSP` 三种通用类型，删除不支持的 AIP 分支
- 默认 `performance_profile` 从 `BALANCED` 改为 `HIGH_PERFORMANCE`（与外部参考一致，降低推理延迟）
- 增加 CPU fallback：`RuntimeList` 追加 `CPU` 作为回退，目标 runtime 不可用时自动降级到 CPU
- 禁用 CPU fallback mode：`builder.setCPUFallbackMode(false)`，避免 SNPE 在 layer 级别自动降级到 CPU，确保 DSP 执行路径纯净

### 2.4 输入零拷贝（消除输入 memcpy 开销）

当前 `Infer()` 中即使复用了 ITensor，仍需要 `std::copy`/`memcpy` 将用户传入的 `Tensor::data` 拷贝到 ITensor 内部缓冲区。通过引入输入零拷贝机制，允许调用方直接写入 ITensor 内部缓冲区，完全消除输入阶段的拷贝。

> **注意**：本节的命名在后续 Proposal-014 中进一步优化——`InputBuffer` 被替换为泛型 `Span<void>`，并增加了对称的 `GetOutputBuffer()`/`GetOutputInfoAt()`/`GetOutputTensor()` 接口。详见 `docs/proposals/014-backend-interface-symmetry-and-span.md`。

**接口设计**（在 `IBackend` 基类中）：

```cpp
// 1. 按索引查询单个输入信息（避免全量 vector 拷贝）
virtual utils::TensorInfo GetInputInfoAt(size_t index) const;

// 2. 获取 ITensor 内部缓冲区
virtual Span<void> GetInputBuffer(size_t index) const;

// 3. 便捷方法：一步返回可直接写入的 Tensor
utils::Tensor GetInputTensor(size_t index) const;
```

`GetInputTensor()` 内部组合 `GetInputBuffer()` + `GetInputInfoAt()`，返回的 `Tensor::data` 即 ITensor 内部缓冲区指针，`owns_data = false`。调用方写入数据后，直接传给 `Infer()`：

```cpp
// 零拷贝输入流程
auto input = backend->GetInputTensor(0);
std::memcpy(input.data, my_image, input.byte_size);
backend->Infer({input}, outputs);  // Infer() 内检测到相同指针时跳过拷贝
```

**性能收益**：
- 消除 `Infer()` 内部的 `std::copy`/`memcpy`（输入数据量越大收益越明显）
- `GetInputTensor()` 使用 `GetInputInfoAt()` 而非 `GetInputInfo()`，避免全量 `vector<TensorInfo>` 拷贝
- 返回值 NRVO 优化，零额外 move 开销

### 2.5 输出对称接口（Proposal-014 引入）

输出侧增加与输入侧对称的三个方法，使 `IBackend` 接口形成完整闭环：

```cpp
// 按索引查询输出元数据（默认委托 GetOutputInfo()）
virtual utils::TensorInfo GetOutputInfoAt(size_t index) const;

// 获取输出内部缓冲区（SNPE 后端指向 output_map）
virtual Span<void> GetOutputBuffer(size_t index) const;

// 一步获取指向输出缓冲区的 Tensor
utils::Tensor GetOutputTensor(size_t index) const;
```

**生命周期语义**：`GetOutputTensor()` 返回的 Tensor 在 `Infer()` 前包含垃圾数据，只有 `Infer()` 后才有意义。这与输入侧对称——输入侧数据由调用方写入，输出侧由推理引擎写入。

### 2.5 与外部参考实现的关键架构差异

| 特性 | 外部参考 | Atlas | 说明 |
|------|---------|-------|------|
| ITensor 复用 | 是 | 是 | 已对齐 |
| 输出零拷贝 | 是 | 是 | 已对齐（`output_map` + `owns_data=false`） |
| 输入零拷贝 | 是（外部写入 ITensor 后 `execute`） | 是（`GetInputBuffer` + `GetInputTensor` + `Infer()` 指针对比跳过拷贝）<br>输出侧对称接口详见 Proposal-014 | 已对齐 |
| Runtime 默认 | DSP→CPU 回退 | GPU→CPU 回退 | 目标设备决定 |
| Performance Profile | HIGH_PERFORMANCE | HIGH_PERFORMANCE | 已对齐 |
| CPU Fallback Mode | 未显式设置 | `setCPUFallbackMode(false)` | Atlas 更严格，保证 DSP 执行路径 |
| Context 生命周期 | 每次 pipeline 重建 | 模型常驻共享 | Atlas 更合理 |
| NCHW 检测 | 简单启发式 | 复杂启发式 | 都可优化 |
| 多输入支持 | 不支持 | 支持 | Atlas 完整 |
| Runtime 类型 | 通用 CPU/GPU/DSP | 通用 CPU/GPU/DSP | 已对齐（删除精度变体） |

---

## 三、改动清单

| 文件 | 改动 |
|------|------|
| `src/backend/snpe/snpe_backend_v1.cc` | `SnpeImpl` 新增 `input_tensors`（预分配 ITensor）+ `output_map`（零拷贝输出）；`Load()` 步骤 7 中预创建 ITensor + CPU fallback + 默认 `HIGH_PERFORMANCE` + `setCPUFallbackMode(false)` + 通用 Runtime；`Infer()` 复用 `input_tensors` + 输出零拷贝到 `output_map`；`Unload()` 清理 `input_tensors`；`ParseRuntime()` 删除 AIP 分支，使用通用 CPU/GPU/DSP |
| `src/backend/snpe/snpe_backend_v2.cc` | 同上（同步修改，无 `zdl::` 前缀） |
| `src/backend/snpe/snpe_backend.h` | 无需改动（`SnpeImpl` 定义在 `.cc` 中） |
| `src/backend/snpe/snpe_backend_stub.cc` | 无需改动（空 `SnpeImpl` 兼容） |
| `src/backend/base/i_backend.h` | 1. 新增 `GetInputInfoAt()` 按索引查询单输入元数据（默认委托 `GetInputInfo()`）；<br>2. 新增非虚 `GetInputTensor()` 便捷方法，组合 `GetInputBuffer()` + `GetInputInfoAt()` 一步返回 Tensor；<br>3. 更新关联的 Proposal-014 中 `InputBuffer` 替换为 `Span<void>` + 新增 `GetOutputBuffer`/`GetOutputInfoAt`/`GetOutputTensor` |

---

## 四、验证方案

1. **功能等价性**：同一 DLC 模型 + 同一输入，对比优化前后输出差异 < 1e-5
2. **性能对比**：分别运行 100 次推理，记录 p50/p95/p99 延迟
3. **与外部参考对标**：在相同硬件（QCS6125 DSP）上，相同模型 + 输入，对比推理延迟
4. **回归测试**：确认优化后 manifest、无 SNPE SDK 编译、ARM/Android 交叉编译不受影响

---

## 五、已解决的问题

1. CPU fallback 路径：已通过 `RuntimeList` 直接追加 `CPU` 实现，同时使用 `setCPUFallbackMode(false)` 禁用 layer 级自动降级
2. AIP 支持：确认 SNPE 1.x SDK 不支持通用 AIP Runtime，已从 `ParseRuntime()` 中删除，默认回退 GPU
3. 输出零拷贝安全性：通过 `owns_data = false` 标记 + `output_map` 生命周期绑定确保正确性
4. 输入零拷贝便利接口：`IBackend` 新增 `GetInputTensor()` + `GetInputInfoAt()`，调用方可一步获取可写入的 Tensor，`Infer()` 内通过指针对比跳过输入数据拷贝。使用 `GetInputInfoAt()` 替代 `GetInputInfo()` 避免全量 vector 拷贝
5. 输出接口对称性：详见 Proposal-014，引入 `Span<void>` 替代 `InputBuffer`，新增 `GetOutputBuffer()`/`GetOutputInfoAt()`/`GetOutputTensor()` 与输入侧一一对应
