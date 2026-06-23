# Unified Vision Runtime Framework (UVR) 开发需求与技术规范

## 1. 项目定位

**Unified Vision Runtime Framework（UVR）** 是一个统一视觉模型运行时抽象层（Runtime Abstraction Layer），用于屏蔽不同推理后端（ONNX Runtime、SNPE、RKNN、QNN 等）的差异，为业务层提供统一的模型管理、配置解析、Session 调度和 Tensor 数据结构。

### 核心目标

```text
Application
      ↓
Runtime Core
      ↓
Platform Backend
      ↓
SessionPool
      ↓
Session
      ↓
Inference Engine
```

## 2. 非目标（Out of Scope）

本项目明确不实现以下能力：

- Graph（计算图）
- Operator（通用算子框架）
- Kernel（算子实现）
- Compiler（编译器）
- Scheduler（图调度器）
- 自定义算子系统
- 推理引擎本身

本框架仅负责：

- 模型管理
- 配置解析
- Tensor 抽象
- Session 管理
- Backend 适配
- 推理调用统一接口

---

# 3. 核心架构

## 3.1 Runtime 层级关系

```text
Runtime
├── PlatformBackend
│      └── SessionPool
└── OrtBackend（Optional Fallback）
       └── SessionPool
```

约束：

1. 一个 Runtime 实例仅绑定一个主 Backend。
2. 主 Backend 由目标平台决定：

| 平台 | 主 Backend |
|------|------------|
| Qualcomm | SNPE 或 QNN |
| Rockchip | RKNN |
| 通用 Linux/Android | ONNX Runtime |

3. 可选支持一个 OrtBackend 作为 Fallback。
4. 不实现动态插件发现机制。
5. 不实现跨厂商 Backend 共存调度。

---

# 4. AnyTensor 统一数据结构

## 4.1 DType

```cpp
enum class DType {
    UINT8,
    INT8,
    INT16,
    INT32,
    INT64,
    FLOAT16,
    FLOAT32,
    FLOAT64
};
```

## 4.2 DeviceType

```cpp
enum class DeviceType {
    CPU,
    GPU,
    DSP,
    HTP,
    NPU
};
```

## 4.3 MemoryType

```cpp
enum class MemoryType {
    Host,
    Device,
    Shared,
    DmaBuf
};
```

## 4.4 Layout

```cpp
enum class Layout {
    NCHW,
    NHWC,
    CHW,
    HWC,
    NC,
    UNKNOWN
};
```

## 4.5 AnyTensor

```cpp
struct AnyTensor {
    void* data;

    std::vector<int64_t> shape;

    DType dtype;

    DeviceType device;

    MemoryType memory;

    Layout layout;

    size_t bytes;
};
```

约束：

- AnyTensor 是唯一公共 Tensor 数据结构。
- 公共接口禁止泄漏：
  - OrtValue
  - ITensor
  - UserBuffer
  - rknn_tensor_mem
  - Qnn_Tensor_t

---

# 5. Session 抽象

## 定义

Session 表示：

> 一个模型运行实例（Model Inference Instance）。

例如：

```text
det.onnx -> Session
face.dlc -> Session
pose.rknn -> Session
```

一个 Backend 可以创建多个 Session。

## 接口

```cpp
class ISession {
public:
    virtual ~ISession() = default;

    virtual Status Run(
        const TensorMap& inputs,
        TensorMap& outputs) = 0;

    virtual const ModelMeta&
        Meta() const = 0;

    virtual SessionCapability
        Capability() const = 0;
};
```

## SessionCapability

```cpp
struct SessionCapability {
    bool thread_safe;
    bool support_async;
    bool support_batch;
};
```

---

# 6. Session 并发策略

由于：

- ORT 大部分情况下支持并发；
- SNPE 部分版本不保证线程安全；
- RKNN 不建议同 Session 并发调用；

必须支持：

```cpp
enum class SessionPolicy {
    Shared,
    ThreadLocal,
    SessionPool
};
```

推荐默认：

```text
SessionPool
```

## SessionPool

```cpp
class SessionPool {
public:
    SessionPtr Acquire();
    void Release(SessionPtr);
};
```

---

# 7. Backend 抽象

## 定义

Backend 表示：

> 平台推理 Runtime Provider。

例如：

```text
OrtBackend
SnpeBackend
RknnBackend
QnnBackend
```

## 接口

```cpp
class IBackend {
public:
    virtual ~IBackend() = default;

    virtual Status Init() = 0;

    virtual SessionPtr CreateSession(
        const ModelConfig&) = 0;

    virtual BackendInfo
        Info() const = 0;
};
```

## Backend 与 Session 关系

```text
1 Backend
      ↓
N Sessions
```

例如：

```text
OrtBackend
 ├── det_session
 ├── rec_session
 └── ocr_session
```

---

# 8. 模型配置管理

参考 InsightFace 的 YAML 风格。

## 推荐目录结构

```text
models/
├── face_det/
│   ├── model.onnx
│   └── config.yaml
├── face_rec/
│   ├── model.dlc
│   └── config.yaml
└── pose/
    ├── model.rknn
    └── config.yaml
```

## 推荐 YAML

```yaml
name: face_det

backend: ort

model: model.onnx

session:
  device: cpu
  threads: 4
  pool_size: 2
  lazy_load: true

inputs:
  - name: input
    shape: [1,3,640,640]
    dtype: fp32
    layout: nchw

outputs:
  - name: scores
  - name: boxes
  - name: kps
```

### SNPE 示例

```yaml
name: face_rec

backend: snpe

model: model.dlc

session:
  runtime: dsp
  pool_size: 2
  user_buffer: true
  enable_cache: true
```

### RKNN 示例

```yaml
name: pose

backend: rknn

model: model.rknn

session:
  core_mask: auto
```

---

# 9. Model Registry

```cpp
class ModelRegistry {
public:
    Load(path);

    Get(name);

    Exists(name);

    Reload(name);
};
```

必须支持：

- 模型发现
- 配置解析
- 多模型加载
- 热更新
- 多版本共存

---

# 10. Runtime Facade

业务层禁止直接接触任何 Backend SDK 类型。

统一接口：

```cpp
runtime.Run(
    model_name,
    inputs,
    outputs);
```

建议：

```cpp
class Runtime {
public:
    LoadModels(path);

    GetSession(name);

    Run(
        name,
        inputs,
        outputs);
};
```

---

# 11. 工程约束

## Core 禁止依赖第三方推理 SDK

禁止：

```cpp
#include <onnxruntime_cxx_api.h>
#include <SNPE.hpp>
#include <rknn_api.h>
#include <QnnInterface.h>
```

仅允许：

```text
backends/*
```

依赖第三方 SDK。

---

# 12. 推荐目录结构

```text
runtime/

├── include/
│   └── runtime/

├── core/
│   ├── any_tensor/
│   ├── backend/
│   ├── session/
│   ├── registry/
│   ├── config/
│   └── runtime/

├── backends/
│   ├── ort/
│   ├── snpe/
│   ├── rknn/
│   └── qnn/

├── models/

├── third_party/

└── tests/
```

---

# 13. AI 开发规范（直接作为 Prompt 使用）

```text
1. 使用 C++17。
2. 使用 RAII 和智能指针。
3. Core 与 Backend 强解耦。
4. 所有公共能力通过抽象接口定义。
5. AnyTensor 是唯一公共 Tensor 数据结构。
6. 一个 Backend 可以创建多个 Session。
7. Session 生命周期独立于 Backend。
8. Session 支持 Shared、ThreadLocal、SessionPool 策略。
9. 配置采用 InsightFace 风格 YAML。
10. Runtime 仅负责：
    - Tensor 统一
    - 模型管理
    - 配置解析
    - Session 管理
    - Backend 抽象
11. Runtime 不负责：
    - Graph
    - Operator
    - Kernel
    - Compiler
    - 自定义算子
12. Core 禁止依赖任何推理 SDK。
13. Backend SDK 类型不得泄漏到公共 API。
14. 优先生成可测试、低耦合、接口优先的代码。
15. 所有实现先定义接口与数据结构，再实现 Backend Adapter。
```

## 项目定义

> A lightweight, plugin-based unified inference runtime abstraction framework for heterogeneous AI backends.
