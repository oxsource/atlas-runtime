# Proposal-017: Pipeline Buffer Reuse

> **提议日期**：2026-07-14
> **提议人**：AI
> **状态**：已采纳
> **类型**：模块级
> **关联**：`src/pipeline/pipeline.cc`、`src/pipeline/pipeline_node.h`、`src/public/include/atlas/types.h`、`src/pipeline/nodes/*.cc`、`src/api/model_handle.cc`、`src/backend/base/i_backend.h`

---

## 一、背景与动机

### 1.1 现状

当前 `Pipeline::Run()` 使用 ping-pong buffer 策略在两个预分配的 `Tensor`（`buf_a` / `buf_b`）之间交替，作为每个 pipeline node 的 `output`。

```cpp
utils::Tensor buf_a;
utils::Tensor buf_b;

const utils::Tensor* cur_in = &input;
utils::Tensor*       cur_out = &buf_a;

for (size_t i = 0; i < nodes_.size(); ++i) {
    auto ret = nodes_[i]->Process(ctx, *cur_in, cur_out);
    if (ret != utils::ErrorCode::kOk) return ret;
    if (i + 1 < nodes_.size()) {
        cur_in  = cur_out;
        cur_out = (cur_out == &buf_a) ? &buf_b : &buf_a;
    }
}
*output = std::move(*cur_out);
```

每个节点在 `Process()` 内部都通过 `malloc()` 分配 output buffer，然后 Pipeline 层通过 `std::move` 将 final output 移交。

### 1.2 问题

1. **重复 malloc/free**：对于 N 个输出尺寸相同的连续节点，每个节点都 `malloc()` 分配新 buffer、前一个节点的 buffer 在 `Tensor` 移动后被 `free()`。分配次数为 O(N)，但理论上只需要 O(1) 次。

2. **无法复用已有的 buffer**：`Tensor` 没有区分"已分配容量"和"实际数据大小"。即使 `buf_a` 已有足够大的 buffer，节点也无法判断能否复用，只能重新 `malloc`。

3. **Pipeline 不知道哪个是最后一个节点**：swap 逻辑只是机械地交替使用 buf_a/buf_b，最后一个节点的 output 还需要通过 `std::move(*cur_out)` 再移一次到最终 output，多一次指针转移。

4. **Input pipeline → Infer 之间的多余拷贝**：Input pipeline 的最后一个节点写入 `preprocessed`，然后 `Infer()` 再将 `preprocessed.data` 拷贝到 backend 内部 buffer。如果最后一个节点直接写入 backend 的 `GetInputBuffer()`，可以省掉这次拷贝。

   ```
   raw_input → BGR→RGB → Normalize → HWC→CHW (last node)
                                       ↓
                                 preprocessed (malloc)
                                       ↓
                                 Infer() 拷贝到 backend 内部 buffer  ← 多余
   ```

5. **Infer → output pipeline → 最终输出的多余拷贝**：Output pipeline 的第一个节点从 `raw_outputs[i]`（backend 内部 buffer）拷贝到 `postprocessed`，但最后一个节点的 output 最终也可能需要拷到 `outputs[]`。

### 1.3 示例分析

以典型的预处理管线 `BGR→RGB → Normalize → HWC→CHW → Resize(300x300)` 为例：

| 阶段 | 当前行为 | 分配次数 |
|------|---------|---------|
| BGR→RGB | buf_a = malloc(640x480x3) | 1 |
| Normalize | buf_b = malloc(640x480x3) | 1 |
| HWC→CHW | buf_a 被 free → 再次 malloc(640x480x3) | 1 |
| Resize(300x300) | buf_b 被 free → malloc(300x300x3) | 1 |
| **总计（仅 pipeline 内部）** | | **4 次** |

加上 `Infer()` 前后的拷贝（假设开启了 input pipeline 和 output pipeline）：

```
[Input Pipeline 最后一个节点]  malloc preprocessed
[Infer]                        preprocessed → backend 内部 buffer (拷贝)
[Infer]                        backend 内部 buffer → raw_outputs (拷贝/所有权转移)
[Output Pipeline 第一个节点]    raw_outputs → output pipeline 内部 buf_a (拷贝/malloc)
```

---

## 二、方案设计

### 2.1 整体思路

从三个方向解决：

1. **Tensor 增加 `capacity` 字段 + `EnsureCapacity()` 方法**，让节点可以复用已有 buffer
2. **Context 增加只读的 pipeline 位置信息 + `endpoint()` 判断**，让节点能判断自己在 pipeline 中的位置
3. **Context 增加 `GetInputTensor()` / `GetOutputTensor()` 方法**，让节点方便地获取 backend buffer 的 Tensor 视图（含 info），数据写入后零拷贝进入 Infer

Pipeline 层的 swap 逻辑基本不变，buffer 复用决策下沉到各节点的 `Process()` 内部。

### 2.2 Tensor 结构变更

```cpp
// src/public/include/atlas/types.h
struct Tensor {
    TensorInfo info;
    void* data = nullptr;
    size_t byte_size = 0;      // 实际有效数据大小
    size_t capacity = 0;       // 已分配的内存容量（新增）
    bool owns_data = false;

    // 确保至少有 size 字节的可写空间。必要时 realloc。
    // 分配失败时保留原有状态不变。
    void EnsureCapacity(size_t size) {
        if (capacity >= size) {
            byte_size = size;  // 统一更新 byte_size
            return;            // 足够，直接复用
        }
        void* new_data = malloc(size);
        if (!new_data) return;  // 分配失败 — 保留现有状态
        if (owns_data && data) free(data);
        data = new_data;
        capacity = size;
        byte_size = size;
        owns_data = true;
    }

    // 原有的构造/析构/移动方法...（capacity 同步处理）
};
```

**`EnsureCapacity()` 的行为**：
- 如果 `capacity >= size`：**直接返回，不清零 buffer**。调用节点必须保证全量覆盖写入新数据。复用场景下 buffer 中可能残留旧数据，但调用节点会覆盖所有字节。
- 如果 `capacity < size`：释放旧内存，`malloc(size)`，更新 `capacity`
- `owns_data == false`（借用外部 buffer）时：同样 `free` + `malloc`，因为此时节点需要可写 buffer

**数据重置约定（重要）**：
> `EnsureCapacity()` **不会**将缓冲区清零或重置。调用节点 **必须完全覆盖** `[0, size)` 范围内的所有字节。
> 这是有意的设计选择——避免不必要的 memset 开销。所有 pipeline 节点目前都是全量写入操作（BGR→RGB 写全部像素，Normalize 写全部通道，Resize 写全部输出像素等），不存在"只写部分"的情况。
> 未来如果引入只写部分数据的节点，该节点自身负责处理未被覆盖区域的清零。

### 2.3 Context 变更：private 位置信息 + endpoint() + 便利方法

```cpp
// src/pipeline/pipeline_node.h
struct Context {
    const core::ModelConfig*  config   = nullptr;
    const backend::IBackend*  backend  = nullptr;
    utils::UserData           user;

    // ── I/O index（只读）────────────────────────────────────
    // 当前处理的是 manifest 中的第几个 input/output。
    // 由 ModelHandle::Run() 设置，节点只读。
    size_t input_index()  const { return input_index_; }
    size_t output_index() const { return output_index_; }

    // ── Pipeline 位置信息（只读）────────────────────────────
    // 当前节点在 pipeline 中的位置 (0-based)。
    // 由 Pipeline::Run() 在遍历时设置，节点只读。
    size_t node_index() const { return node_index_; }
    size_t node_count() const { return node_count_; }

    // 判断当前节点是否为 pipeline 的最后一个节点（端点）。
    bool endpoint() const {
        return node_count_ > 0 && node_index_ == node_count_ - 1;
    }

    // ── 便利方法：获取 backend 的零拷贝 buffer ──────────────

    // |force| 为 true 时，尝试将 |tensor| 填充为 backend 输入 buffer 的
    // Tensor 视图（零拷贝进入 Infer），成功返回 true。
    // 当 backend 不支持时 |tensor| 不变，返回 false。
    // |force| 为 false 时直接返回 false，不修改 |tensor|。
    bool GetInputTensor(utils::Tensor& tensor, bool force) const {
        if (!force) return false;
        if (backend == nullptr) return false;
        auto buf = backend->GetInputBuffer(input_index_);
        if (buf.data == nullptr) return false;
        auto info = backend->GetInputInfoAt(input_index_);
        tensor.data      = buf.data;
        tensor.byte_size = buf.size;
        tensor.capacity  = buf.size;
        tensor.owns_data = false;
        tensor.info      = std::move(info);
        return true;
    }

    // |force| 为 true 时，尝试将 |tensor| 填充为 backend 输出 buffer 的
    // Tensor 视图（零拷贝后处理），成功返回 true。
    // 当 backend 不支持时 |tensor| 不变，返回 false。
    // |force| 为 false 时直接返回 false，不修改 |tensor|。
    bool GetOutputTensor(utils::Tensor& tensor, bool force) const {
        if (!force) return false;
        if (backend == nullptr) return false;
        auto buf = backend->GetOutputBuffer(output_index_);
        if (buf.data == nullptr) return false;
        auto info = backend->GetOutputInfoAt(output_index_);
        tensor.data      = buf.data;
        tensor.byte_size = buf.size;
        tensor.capacity  = buf.size;
        tensor.owns_data = false;
        tensor.info      = std::move(info);
        return true;
    }

 private:
    friend class Pipeline;
    friend class api::ModelHandle;
    size_t input_index_  = 0;
    size_t output_index_ = 0;
    size_t node_index_   = 0;
    size_t node_count_   = 0;
};
```

**保护设计**：
- `input_index_` / `output_index_` / `node_index_` / `node_count_` 均为 **private** 成员，节点不可修改
- `Pipeline` 和 `ModelHandle` 作为 **friend class** 有权写入
- 节点通过 **只读 getter** `input_index()` / `output_index()` / `node_index()` / `node_count()` 读取
- `endpoint()` 提供便捷判断，封装了 `node_index_ == node_count_ - 1` 逻辑
- `GetInputTensor()` / `GetOutputTensor()` 不判断 `endpoint()`——开发者自行决定是否使用，通用性更强
- backend 不支持时返回空 Tensor（`data == nullptr`），调用方回退到 `EnsureCapacity()`

#### 2.3.1 节点中的使用示例

`GetInputTensor()` / `GetOutputTensor()` 不再耦合 `endpoint()` 判断。调用方通过 `force` 参数控制是否尝试获取 backend buffer：

- `force=true`：尝试获取，成功返回 `true` 并填充 `tensor`（info 已由 backend 设定）；失败时 `tensor` 不变，返回 `false`
- `force=false`：直接返回 `false`，**不修改** `tensor`（中间节点可在设置好 `info` 后安全跳过）

**Input pipeline 最后一节点（如 HWCToCHWNode）**：
```cpp
utils::ErrorCode HWCToCHWNode::Process(const Context& ctx,
                                       const utils::Tensor& input,
                                       utils::Tensor* output) {
    // ... 校验逻辑 ...

    // endpoint 时尝试零拷贝；非 endpoint 或失败时走 fallback
    if (!ctx.GetInputTensor(*output, ctx.endpoint())) {
        output->info = input.info;
        output->EnsureCapacity(input.byte_size);
    }

    // 执行 HWC→CHW 转换，写入 output->data
    // ...
    return utils::ErrorCode::kOk;
}
```

**Output pipeline 最后一节点（如 SoftmaxNode）**—— 需要自定义 shape（如 TopKNode）时：
```cpp
utils::ErrorCode TopKNode::Process(const Context& ctx,
                                   const utils::Tensor& input,
                                   utils::Tensor* output) {
    size_t out_bytes = static_cast<size_t>(k_) * sizeof(float);

    // endpoint 时尝试零拷贝；失败后需自定义 info + 分配
    if (!ctx.GetOutputTensor(*output, ctx.endpoint())) {
        output->info       = input.info;
        output->info.shape = {k_};
        output->EnsureCapacity(out_bytes);
    }

    // 执行 top-k 运算，写入 output->data
    // ...
    return utils::ErrorCode::kOk;
}
```

**中间节点** —— 直接传 `false`，跳过零拷贝尝试：
```cpp
// 中间节点：不尝试拿后端 buffer，仅 fallback 分配
output->info = input.info;
output->EnsureCapacity(input.byte_size);
```

### 2.4 Pipeline::Run() 变更

```cpp
utils::ErrorCode Pipeline::Run(const utils::Tensor& input,
                              utils::Tensor* output,
                              const IPipelineNode::Context& ctx) const {
    if (output == nullptr) return utils::ErrorCode::kInvalidArgument;
    if (nodes_.empty()) {
        output->info      = input.info;
        output->data      = input.data;
        output->byte_size = input.byte_size;
        output->capacity  = input.capacity;
        output->owns_data = false;
        return utils::ErrorCode::kOk;
    }

    utils::Tensor buf_a;
    utils::Tensor buf_b;

    const utils::Tensor* cur_in = &input;
    utils::Tensor*       cur_out = &buf_a;

    for (size_t i = 0; i < nodes_.size(); ++i) {
        IPipelineNode::Context node_ctx = ctx;
        // friend class 访问 private 成员
        node_ctx.node_index_ = i;
        node_ctx.node_count_ = nodes_.size();

        // 最后一个节点直接写入最终 output，跳过 final move
        utils::Tensor* target = (i + 1 == nodes_.size()) ? output : cur_out;

        auto ret = nodes_[i]->Process(node_ctx, *cur_in, target);
        if (ret != utils::ErrorCode::kOk) return ret;

        if (i + 1 < nodes_.size()) {
            cur_in  = target;
            cur_out = (cur_out == &buf_a) ? &buf_b : &buf_a;
        }
    }

    return utils::ErrorCode::kOk;
}
```

关键变化：
- 最后一个节点直接写 `output`，无需 `std::move`
- `Context` 中通过 friend 访问填充 `node_index_` / `node_count_`

### 2.5 ModelHandle::Run() 变更

```cpp
// ModelHandle::Run() 中构造 Context 时设置 input_index_ / output_index_
pipeline::IPipelineNode::Context ctx;
ctx.config    = &entry_->config;
ctx.backend   = entry_->backend.get();
ctx.user      = cfg;

// input pipeline（单输入重载）
ctx.input_index_ = 0;
ctx.user.flags |= pipeline::kPipeFlagInputPipe;

// input pipeline（多输入重载）
for (size_t i = 0; i < raw_inputs.size(); ++i) {
    ctx.input_index_ = i;   // friend 类可直接访问 private 成员
    // ...
}

// output pipeline
for (size_t i = 0; i < raw_outputs.size(); ++i) {
    ctx.output_index_ = i;
    ctx.user.flags = (cfg.flags & ~0x03) | pipeline::kPipeFlagOutputPipe;
    // ...
}
```

Input pipeline 的最后一节点直接写 `backend->GetInputBuffer()` 后，`Infer()` 接收同一 data 指针，backend 识别到数据已在内部 buffer 时不再拷贝。

Output pipeline 同理：最后一节点直接引用 `backend->GetOutputBuffer()`，`raw_outputs[i]` 和 `postprocessed.data` 指向同一块内存。

### 2.6 各节点 `Process()` 的改造（基础 EnsureCapacity）

所有节点将 `malloc()` + `owns_data = true` 替换为 `output->EnsureCapacity()`。

**改造前（以 BGRToRGBNode 为例）**：
```cpp
output->info      = input.info;
output->byte_size = input.byte_size;
output->data      = malloc(input.byte_size);
output->owns_data = true;
```

**改造后**：
```cpp
output->info = input.info;
output->EnsureCapacity(input.byte_size);
```

改造涉及的节点（9 个）：

| 节点 | 替换内容 |
|------|---------|
| `BGRToRGBNode` | `malloc()` → `EnsureCapacity()` |
| `RGBToBGRNode` | 同上 |
| `NormalizeNode` | 同上 + 可选用 `GetInputTensor()` + `endpoint()` |
| `HWCToCHWNode` | 同上 + 可选用 `GetInputTensor()` + `endpoint()` |
| `CHWToHWCNode` | 同上 |
| `DTypeConvertNode` | 同上 + 可选用 `GetInputTensor()` + `endpoint()` |
| `SoftmaxNode` | 同上 + 可选用 `GetOutputTensor()` + `endpoint()` |
| `TopkNode` | 同上 + 可选用 `GetOutputTensor()` + `endpoint()` |
| `ResizeNode` | 同上 |

### 2.7 效果分析

#### 场景 1: 纯 input pipeline + Infer（典型预处理管线）

`BGR→RGB → Normalize → HWC→CHW → Infer`

| 阶段 | 当前行为 | 改后行为 |
|------|---------|---------|
| BGR→RGB | buf_a = malloc(640x480x3) | buf_a = malloc(640x480x3) |
| Normalize | buf_b = malloc(640x480x3) | buf_b = malloc(640x480x3) |
| HWC→CHW | buf_a free → malloc(640x480x3) | buf_a 容量足够 → **无分配** |
| Infer | preprocessed → backend 内部 buffer（拷贝） | 数据已在 backend buffer → **零拷贝** |
| **分配次数** | **4 次** | **2 次** |
| **数据拷贝次数** | **3 次（节点间） + 1 次（pipeline→backend）** | **2 次（节点间）** |

#### 场景 2: 纯同尺寸管线（无 resize）

分配次数从 N+1 次降到 2 次（buf_a + buf_b 各一次），且 Infer 零拷贝。

#### 场景 3: 含 resize 管线

`BGR→RGB → Normalize → Resize(300x300) → HWC→CHW → Infer`

| 阶段 | 当前行为 | 改后行为 |
|------|---------|---------|
| BGR→RGB | buf_a = malloc(640x480x3) | buf_a = malloc(640x480x3) |
| Normalize | buf_b = malloc(640x480x3) | buf_b 容量足够 → **无分配** |
| Resize | buf_a free → malloc(300x300x3) | buf_a 容量不够 → **realloc** |
| HWC→CHW | buf_b free → malloc(300x300x3) | buf_b 容量不够 → **realloc** |
| Infer | preprocessed → backend（拷贝） | 数据已在 backend buffer → **零拷贝** |
| **分配次数** | **5 次** | **3 次** |

### 2.8 向后兼容性

- `Tensor::capacity`：新增字段，已有二进制加载的 Tensor（`owns_data = false`）默认为 `capacity = 0`，首次 `EnsureCapacity()` 时正确触发 `malloc()`
- `Context` 新增 getter 方法（`input_index()` / `output_index()` / `node_index()` / `node_count()` / `endpoint()` / `GetInputTensor()` / `GetOutputTensor()`）：默认返回 0 / false / 空 Tensor，现有直接调用 `Process()` 的代码不受影响
- `Pipeline::Run()` 接口签名不变，调用方无需修改

### 2.9 安全性和约束

1. **backend buffer 的生命周期**：`GetInputBuffer()` / `GetOutputBuffer()` 返回的 buffer 在 `Infer()` 完成后可能失效。Output pipeline 的最后一节点必须在 `Infer()` 返回后（即 `ModelHandle::Run()` 中）执行，此时 buffer 仍然有效。
2. **output pipeline 持有后端 buffer 引用**：`ModelHandle::Run()` 中 `raw_outputs[i]` 是 backend 的内部 buffer，output pipeline 最后一节点直接引用它后，`postprocessed` 不拥有数据。`outputs->push_back(std::move(postprocessed))` 只是转移了 Tensor 元数据，不影响数据指针。
3. **不支持 `GetInputBuffer()` / `GetOutputBuffer()` 的 backend**：返回空 Span，`GetInputTensor()` / `GetOutputTensor()` 返回空 Tensor，调用方回退到 `EnsureCapacity()`
4. **Context private 成员的保护**：`input_index_` / `output_index_` / `node_index_` / `node_count_` 只能通过 `Pipeline` 和 `ModelHandle`（friend class）写入，节点无法篡改

---

## 三、改动清单

| 文件 | 改动 |
|------|------|
| `src/public/include/atlas/types.h` | `Tensor` 增加 `capacity` 字段 + `EnsureCapacity()` 方法；构造/析构/移动方法同步处理 `capacity` |
| `src/pipeline/pipeline_node.h` | `Context` 改为 private 成员 + 只读 getter + `endpoint()` + `GetInputTensor()` + `GetOutputTensor()`；声明 `Pipeline` 和 `ModelHandle` 为 friend |
| `src/pipeline/pipeline.cc` | `Run()` 中通过 friend 访问填充 `node_index_`/`node_count_`；最后一个节点直接写入 `output`，移除 `std::move(*cur_out)` |
| `src/api/model_handle.cc` | 通过 friend 访问设置 `input_index_`/`output_index_` |
| `src/pipeline/nodes/bgr_to_rgb_node.cc` | `malloc()` → `output->EnsureCapacity()` |
| `src/pipeline/nodes/rgb_to_bgr_node.cc` | 同上 |
| `src/pipeline/nodes/normalize_node.cc` | 同上 + 可选用 `GetInputTensor()` + `endpoint()` |
| `src/pipeline/nodes/hwc_to_chw_node.cc` | 同上 + 可选用 `GetInputTensor()` + `endpoint()` |
| `src/pipeline/nodes/chw_to_hwc_node.cc` | 同上 |
| `src/pipeline/nodes/dtype_convert_node.cc` | 同上 + 可选用 `GetInputTensor()` + `endpoint()` |
| `src/pipeline/nodes/softmax_node.cc` | 同上 + 可选用 `GetOutputTensor()` + `endpoint()` |
| `src/pipeline/nodes/topk_node.cc` | 同上 + 可选用 `GetOutputTensor()` + `endpoint()` |
| `src/pipeline/nodes/resize_node.cc` | 同上 |

---

## 四、验证方案

1. **编译验证**：`bazel build //...` 通过
2. **已有单元测试**：`bazel test //...` 所有套测试通过
   - `//tests/pipeline:pipeline_test`
   - `//tests/pipeline:pipeline_manifest_test`
3. **行为验证**：
   - 单节点 pipeline：正常运行，output 正确
   - 多节点同尺寸 pipeline：buffer 被复用，无额外分配
   - 多节点变尺寸 pipeline（含 resize）：realloc 正确触发
   - 空 pipeline（nodes_ 为空）：identity 行为不变
   - `Context::endpoint()` 最后一节点返回 true，否则 false
   - `Context::GetInputTensor()` 在支持 backend buffer 时正确返回非空 Tensor；不支持时返回空
   - `Context::GetOutputTensor()` 同理
   - backend 不提供 buffer 的场景：返回空 Tensor，回退到 `EnsureCapacity()` 正常运作
   - 节点无法修改 Context 中的位置信息（编译期检查）
4. **性能验证**：
   - 同尺寸管线：分配次数从 O(N) 降到 O(1)
   - 含 backend buffer 复用的管线：Infer 输入/输出零拷贝

---

## 五、开放问题

1. **`EnsureCapacity()` 的 `owns_data == false` 情况**：如果 output Tensor 借用了外部 buffer（`owns_data = false`），`EnsureCapacity()` 会直接 `free()` 并覆盖走。当前设计中，Pipeline 的 `buf_a`/`buf_b` 是局部变量（`owns_data=false` 初始状态），第一次 `EnsureCapacity()` 时 `data=nullptr, owns_data=false` 会正确 `malloc`。外部传入的 output Tensor 也一定是空的（调用方负责提供空的 output Tensor）。这个行为是安全的。

2. **`capacity` 的拷贝与移动语义**：`Tensor` 拷贝已 delete，移动构造函数和移动赋值运算符需要同步移动 `capacity` 字段。

3. **`Context::GetOutputTensor()` 的 info 来源**：Output pipeline 中 info 来自 `backend->GetOutputInfoAt(output_index_)`，该 info 是 backend 定义的输出 tensor 信息。如果 output pipeline 最后一节点需要不同的 info（例如用户期望的输出 format），节点应在使用 backend Tensor 后自行覆盖 `output->info`。

4. **多 input/output 场景**：`ctx.input_index()` / `ctx.output_index()` 已经支持多输入多输出场景，`GetInputTensor()` / `GetOutputTensor()` 分别使用对应的 index 获取 correct buffer 和 info。

5. **`ResizeNode` 的特殊性**：Resize 的输出尺寸取决于 params 而非 input，`EnsureCapacity()` 仍然适用，因为输入尺寸计算后调用它即可。

6. **`EnsureCapacity()` 不清零 buffer 的安全责任**：调用节点必须全量覆盖 `[0, size)` 范围内的数据。如果节点只写部分字节，尾部的残留旧数据可能导致推理结果异常。所有内置节点（BGR→RGB、Normalize、Resize、HWC→CHW 等）均为全量写入操作，符合此约定。新增的自定义节点需确保同样的全量写入保证。