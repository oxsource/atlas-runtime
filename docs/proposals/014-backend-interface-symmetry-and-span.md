# Proposal-014: IBackend 接口对称性与 Span 统一类型

> **提议日期**：2026-07-09
> **完成日期**：2026-07-09
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已完成
> **类型**：模块级
> **关联**：`docs/proposals/012-snpe-backend-inference-optimization.md`、`src/backend/base/i_backend.h`、`src/utils/span.h`

---

## 一、背景与动机

Proposal-012 为 `IBackend` 新增了 `GetInputBuffer()`、`GetInputInfoAt()`、`GetInputTensor()` 三个输入侧方法，实现了输入零拷贝的便利接口。但输出侧缺乏对应的对称方法，导致：

| 问题 | 说明 |
|------|------|
| **接口不对称** | 输入侧有 `GetInputBuffer`/`GetInputInfoAt`/`GetInputTensor`，输出侧无对应方法 |
| **`InputBuffer` 命名有方向性** | 名为 `InputBuffer` 却无法用于输出侧，不适合作为 `GetOutputBuffer` 的返回类型 |
| **缺少通用「指针+长度」类型** | 项目各模块后续可能也需要此模式，缺少一个可复用的类型定义 |

### 1.1 讨论过程摘要

方案经过多轮迭代：

1. **初案**：`GetOutputBuffer()` 复用 `InputBuffer` 返回值 → 命名冲突
2. **`TensorRawBuffer`**：重命名 `InputBuffer` → `TensorRawBuffer` → 仍需自定义 struct
3. **`std::span`**：C++20 引入，项目使用 C++17 → 不可用
4. **自定义 `Span<T>` 模板**（最终方案）：零依赖，泛用性强，`Span<void>` 直接替代 `InputBuffer`，与 `Tensor::data` 的 `void*` 类型一致

---

## 二、最终方案

### 2.1 新增 `utils::Span<T>`（零依赖泛型模板）

在 `src/utils/span.h` 中定义：

```cpp
template <typename T>
struct Span {
    T*     data = nullptr;
    size_t size = 0;

    bool empty() const noexcept { return size == 0; }
    T& operator[](size_t i) noexcept { return data[i]; }
    const T& operator[](size_t i) const noexcept { return data[i]; }
};
```

- 使用 `size` 而非 `byte_size`，与 `std::span` 风格一致
`Span<void>` 表达原始字节缓冲区，与 `Tensor::data` 的 `void*` 类型直接兼容，布局零开销。
- 后续任意模块可用 `Span<float>`、`Span<int32_t>` 等

### 2.2 删除 `InputBuffer`，统一到 `Span<void>`

```cpp
// ─── 删除 ───
struct InputBuffer {
    void*  data      = nullptr;
    size_t byte_size = 0;
};

// ─── 使用 ───
virtual Span<void> GetInputBuffer(size_t index) const;
```

`InputBuffer` 的所有使用处直接替换为 `Span<void>`，逻辑零变化。

### 2.3 新增输出侧对称接口

在 `IBackend` 中新增三个方法：

```cpp
// ─── 1. 按索引查询输出元数据（默认委托 GetOutputInfo()）───
virtual utils::TensorInfo GetOutputInfoAt(size_t index) const {
    auto infos = GetOutputInfo();
    return index < infos.size() ? std::move(infos[index])
                                : utils::TensorInfo{};
}

// ─── 2. 获取输出内部缓冲区（默认返回空）───
virtual Span<void> GetOutputBuffer(size_t index) const {
    (void)index;
    return {};
}

// ─── 3. 便捷方法：一步返回输出 Tensor ───
utils::Tensor GetOutputTensor(size_t index) const {
    Span<void> buf = GetOutputBuffer(index);
    if (buf.data == nullptr) return {};
    utils::Tensor t;
    t.data      = buf.data;
    t.byte_size = buf.size;       // Span::size 即字节数
    t.owns_data = false;
    t.info      = GetOutputInfoAt(index);
    return t;
}
```

### 2.4 与输入侧对称性总览

| 方法 | 输入侧 | 输出侧 |
|------|:------:|:------:|
| 缓冲指针获取 | `GetInputBuffer(index)` | `GetOutputBuffer(index)` |
| 按索引元数据 | `GetInputInfoAt(index)` | `GetOutputInfoAt(index)` |
| 便捷 Tensor | `GetInputTensor(index)` | `GetOutputTensor(index)` |

### 2.5 `Span<T>` 纳入 Public API

`Span<T>` 作为通用「类型化指针+长度」类型，对外部消费者同样有价值。在推理输出处理、图像数据预处理等场景中，`Span<float>` 可替代手动指针运算：

```cpp
// ─── 之前 ───
const float* data = static_cast<const float*>(outputs[0].data);
const size_t count = outputs[0].byte_size / sizeof(float);
for (size_t i = 0; i < count; ++i) { process(data[i]); }

// ─── 之后 ───
atlas::utils::Span<const float> data(
    static_cast<const float*>(outputs[0].data),
    outputs[0].byte_size / sizeof(float));
for (size_t i = 0; i < data.size(); ++i) { process(data[i]); }
```

**实现方式**：`Span<T>` 定义追加到 `src/public/include/atlas/types.h` 末尾，
与现有 `DataType`、`TensorInfo`、`Tensor` 等类型一同暴露给外部消费者。
公共 API 的 BUILD 已依赖 `//src/utils:types`，`Span<T>` 作为头文件追加不引入额外依赖。

### 2.6 生命周期语义

- `GetOutputBuffer()`/`GetOutputTensor()` 返回的指针绑定到后端内部缓冲区
- **仅在 `Infer()` 调用之后才包含有效推理结果**
- **调用方必须在下次 `Infer()` 之前消费完毕**（`owns_data = false`）
- 这与输入侧的约定一致：输入侧数据由调用方写入后传给 `Infer()`

### 2.7 与 Run / Infer 参数化的关系：双机制互补设计

引入 `GetInputTensor()` / `GetOutputTensor()` 后产生一个自然地设计问题：
既然已经有预分配机制，`Run()` / `Infer()` 是否还需要 `input` 和 `output` 参数？
是否应该改为无参的 `Run()` 调用？

结论是**保留参数化接口，两套机制并存，各司其职**。理由如下：

#### 2.7.1 Pipeline 前后处理依赖 input 参数

`ModelHandle::Run()` 承担的不仅是推理调用，还包括 pipeline 前后处理：

```
raw_input (e.g. HWC uint8)  →  Pipeline::Run()  →  preprocessed (NCHW float32)  →  IBackend::Infer()
```

Pipeline 的输入是原始格式数据，输出才是后端期望的格式。如果 `Run()` 不再接收 `input`
参数，用户必须在外部自行完成预处理，这违背了 `ModelHandle` 封装 pipeline
的核心理念。

#### 2.7.2 多后端兼容性

并非所有后端都支持零拷贝：

- **SNPE 后端**：`Infer()` 内部将输出 `ITensor` 指针直接 push 为 `owns_data=false`
  的 `Tensor`，天然零拷贝输出。
- **CPU 后端（ONNX Runtime）**：每次 `session_->Run()` 返回新的 `Ort::Value`，
  必须 `malloc` + `memcpy` 到 caller 提供的 buffer。不支持 `GetOutputBuffer()`，
  输出数据只能通过 `Infer()` 的 `outputs` 参数返回。

如果 `Infer()` 去掉 `outputs` 参数，CPU 后端的推理结果无处传递。

#### 2.7.3 行业惯例

几乎所有主流推理框架都采用参数化推理接口：

| 框架 | 推理接口 |
|------|---------|
| ONNX Runtime | `session->Run(inputs, outputs)` |
| TensorFlow Lite | `interpreter->Invoke()` + `typed_input_tensor()` / `typed_output_tensor()` |
| SNPE | `snpe->execute(input_tensor, output_map)` |
| ncnn | `extractor.input()` / `extractor.extract()` |

参数化是用户最熟悉的心智模型，降低学习成本。

#### 2.7.4 两种机制的定位

| 路径 | 接口 | 适用场景 |
|------|------|---------|
| **简单路径** | `tensor = 构造数据` → `Run(tensor, &outputs)` | 日常使用，一条语句完成推理 + pipeline，零心智负担 |
| **零拷贝路径** | `GetInputTensor(0)` 写入 → `Run(tensor, &outputs)` → 读取 `outputs[0]` | 性能敏感场景，复用后端内部 buffer，跳过内存拷贝 |

两种路径**可以组合使用**——这正是 `examples/snpe_cpu/main.cc` 展示的模式：
用户通过 `GetInputTensor()` 拿到后端内部 buffer 写入数据，然后将同一个 `Tensor`
传给 `Run()`。SNPE 后端在 `Infer()` 内部通过指针比对检测到用户使用了内部 buffer，
自动跳过输入 `memcpy`。输出侧同理，`Infer()` 将 `owns_data=false` 的 Tensor push
到 `outputs` 中，用户读取时无需额外拷贝。

```cpp
// 零拷贝输入 + 标准 Run() 调用 = 最佳实践
auto input_tensor = model.GetInputTensor(0);     // 拿到 SNPE ITensor buffer
FillData(input_tensor.data);                      // 直接写入
std::vector<Tensor> outputs;
model.Run(input_tensor, &outputs);               // 走 pipeline → Infer
// outputs[0].data 直接指向 SNPE 输出内存
```

#### 2.7.5 小结

`GetInputTensor()` / `GetOutputTensor()` 是**能力暴露**而非**接口替代**。它们让
高级用户在性能敏感场景下可以绕过内存拷贝，但 `Run(input, &outputs)` 始终是 pipeline
编排的标准入口。二者正交互补，不存在冗余。

---

## 三、改动清单

### 3.1 新增文件

| 文件 | 说明 |
|------|------|
| `src/utils/span.h` | `Span<T>` 模板定义（内部模块使用） |

### 3.2 修改文件

| 文件 | 改动 |
|------|------|
| `src/utils/BUILD` | 新增 `cc_library("span")` |
| `src/backend/base/BUILD` | `i_backend` 增加 `//src/utils:span` 依赖 |
| `src/backend/base/i_backend.h` | 删除 `struct InputBuffer`；`InputBuffer` → `Span<void>`；新增 `GetOutputBuffer`、`GetOutputInfoAt`、`GetOutputTensor` |
| `src/backend/snpe/snpe_backend.h` | `InputBuffer` → `Span<void>` |
| `src/backend/snpe/snpe_backend_v1.cc` | `InputBuffer` → `Span<void>`；新增 `GetOutputBuffer()` 实现指向 `output_map`；`GetInputBuffer()` 返回类型替换 |
| `src/backend/snpe/snpe_backend_v2.cc` | 同上（无 `zdl::` 前缀版本） |
| `src/backend/snpe/snpe_backend_stub.cc` | `InputBuffer` → `Span<void>`；新增 `GetOutputBuffer()` 空实现 |
| `src/backend/base/profiling_backend.h` | `InputBuffer` → `Span<void>`；新增 `GetOutputBuffer()` 声明 |
| `src/backend/base/profiling_backend.cc` | `InputBuffer` → `Span<void>`；新增 `GetOutputBuffer()` 转发到 `inner_` |
| `src/public/include/atlas/types.h` | 末尾追加 `Span<T>` 定义（public API 暴露） |
| `docs/proposals/012-snpe-backend-inference-optimization.md` | 同步更新 `InputBuffer` → `Span<void>`，新增输出对称接口说明 |

### 3.3 演示性更新（并非迁移，而是展示新 API 风格）

以下文件不涉及 `InputBuffer` 迁移，但将手动指针运算改为 `Span<T>` 容器式访问，展示 `Span` 的用法：

| 文件 | 改动 |
|------|------|
| `tests/backend/cpu/cpu_backend_test.cc` | `const float* data` + `count` → `Span<const float>` 类型化访问 |
| `tests/api/atlas_runtime_test.cc` | 同上输出数据访问 |
| `examples/snpe_cpu/main.cc` | 输出数据改为 `Span<const float>` 访问 |
| `examples/shared_library/main.cc` | 输出数据改为 `Span<const float>` 访问 |
| `benchmarks/infer_benchmark/benchmark_main.cc` | 无改动（benchmark 不需要类型化数据访问） |

### 3.3 `i_backend.h` 最终接口形态

```cpp
class IBackend {
 public:
    virtual ~IBackend() = default;

    // ── 生命周期 ──
    virtual utils::ErrorCode Load(const std::string& model_path,
                                   const core::ModelConfig& config,
                                   IBackendContext* ctx = nullptr) = 0;
    virtual void Unload() = 0;
    virtual bool IsLoaded() const = 0;

    // ── 推理 ──
    virtual utils::ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                                    std::vector<utils::Tensor>& outputs) = 0;

    // ── 元数据查询 ──
    virtual std::vector<utils::TensorInfo> GetInputInfo() const = 0;
    virtual std::vector<utils::TensorInfo> GetOutputInfo() const = 0;

    virtual utils::TensorInfo GetInputInfoAt(size_t index) const;
    virtual utils::TensorInfo GetOutputInfoAt(size_t index) const;

    // ── 零拷贝缓冲 ──
    virtual Span<void> GetInputBuffer(size_t index) const;
    virtual Span<void> GetOutputBuffer(size_t index) const;

    // ── 便捷方法 ──
    utils::Tensor GetInputTensor(size_t index) const;
    utils::Tensor GetOutputTensor(size_t index) const;

    // ── 版本 ──
    virtual std::string Version() const = 0;
};
```

### 3.4 `profiling_backend.h/cc` 新增方法

```cpp
// profiling_backend.h
Span<void> GetOutputBuffer(size_t index) const override;

// profiling_backend.cc
Span<void> ProfilingBackend::GetOutputBuffer(size_t index) const {
    return inner_->GetOutputBuffer(index);
}
```

---

## 四、无需改动的文件

- **`src/backend/cpu/cpu_backend.h/cc`**：CPU 后端未实现 `GetInputBuffer`（使用默认空实现），同理 `GetOutputBuffer` 也使用默认空实现
- **`src/backend/base/i_backend_context.h`**：无关
- **`src/utils/types.h`**：不涉及
- **`src/core/manifest_config.h/cc`**：不涉及
- **`examples/two_model_pipeline/main.cc`**：已使用公共 API，不涉及底层接口改动
- **`benchmarks/infer_benchmark/benchmark_main.cc`**：benchmark 不需要类型化数据访问

---

## 五、兼容性

| 维度 | 说明 |
|------|------|
| **API 兼容性** | `GetInputBuffer` 返回类型从 `InputBuffer` 变为 `Span<void>`，二进制不兼容但源码替换为机械性改动。所有调用方均在同一仓库中一并修改 |
| **行为兼容性** | 数据布局完全一致（`void* data` + `size_t`），无任何行为变化 |
| **后端兼容性** | 未实现输出零拷贝的后端直接使用默认空实现，不影响推理流程 |
