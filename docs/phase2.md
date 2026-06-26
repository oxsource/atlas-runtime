# 阶段二实现方案：CPU 后端（ONNX Runtime）+ 基础 Pipeline

> **文档版本**：1.1.0
> **对应代码版本**：v1.0.0
> **最后更新**：2026-06-26
> **状态**：已实现

## 一、目标与交付物

| 交付物 | 说明 |
|--------|------|
| CPU Backend | 基于 ONNX Runtime 的 `IBackend` 实现，可在 CPU 上执行 .onnx 模型推理 |
| Pipeline 框架 | `IPipelineNode` 抽象接口 + `Pipeline` 编排类 |
| 内置预处理节点 | Resize、Normalize、HWC→CHW、BGR→RGB、DtypeConvert |
| Pipeline 自动构建 | 从 `TensorInfo`（清单描述）自动生成默认预处理管线 |
| 端到端集成测试 | 清单解析 → 模型加载 → 预处理 → 推理 → 验证输出形状 |

---

## 二、三方库选型

### 2.1 ONNX Runtime

| 项目 | 说明 |
|------|------|
| 库名 | [onnxruntime](https://github.com/microsoft/onnxruntime) |
| 版本 | 1.17.3 |
| 引入方式 | `http_archive` 下载官方预编译包（macOS arm64 / Linux x86_64 / Linux aarch64） |
| 头文件路径 | `include/onnxruntime/core/session/onnxruntime_cxx_api.h` |
| 链接产物 | `lib/libonnxruntime.so`（Linux）/ `lib/libonnxruntime.dylib`（macOS） |

**选型理由：**
- 官方提供预编译二进制，Bazel 接入无需从源码编译；
- 支持 CPU / GPU / NPU 等多种 ExecutionProvider，后续切换 CUDA EP 只需修改配置；
- C++ API 稳定，与 `IBackend` 接口映射直接。

### 2.2 OpenCV（可选）

Pipeline 的 Resize 节点需要图像缩放能力。阶段二提供两种实现：

| 实现 | 依赖 | 适用场景 |
|------|------|----------|
| `ResizeNode`（基础版）| 无额外依赖，纯 C++ 双线性插值 | 轻量嵌入式场景 |
| `ResizeNode`（OpenCV 版）| `opencv_core` + `opencv_imgproc` | 精度要求高、需要更多插值方式 |

阶段二默认使用**基础版（无 OpenCV 依赖）**，OpenCV 版通过 Bazel `select()` 按需启用。

---

## 三、模块设计

### 3.1 CPU Backend

**文件：** `src/backend/cpu/`

```
src/backend/cpu/
├── cpu_backend.h
├── cpu_backend.cc
└── BUILD
```

**类结构：**

```cpp
namespace atlas {
namespace backend {

class CpuBackend : public IBackend {
 public:
    CpuBackend();
    ~CpuBackend() override;

    ErrorCode Load(const std::string& model_path,
                   const core::ModelConfig& config) override;
    ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                    std::vector<utils::Tensor>& outputs) override;
    std::vector<utils::TensorInfo> GetInputInfo()  const override;
    std::vector<utils::TensorInfo> GetOutputInfo() const override;
    void  Unload()    override;
    bool  IsLoaded()  const override;

 private:
    // OrtEnv 和 OrtSession 持有推理上下文，生命周期与 CpuBackend 实例绑定。
    std::unique_ptr<Ort::Env>           env_;
    std::unique_ptr<Ort::Session>       session_;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator_;
    std::vector<utils::TensorInfo>      input_info_;
    std::vector<utils::TensorInfo>      output_info_;
    bool                                loaded_ = false;
};

}  // namespace backend
}  // namespace atlas

// At the bottom of cpu_backend.cc:
ATLAS_REGISTER_BACKEND("cpu", atlas::backend::CpuBackend)
```

**关键实现细节：**

| 步骤 | 说明 |
|------|------|
| `Load()` | 创建 `Ort::Env`、设置 `SessionOptions`（线程数从 `config.config["num_threads"]` 读取），加载模型文件 |
| `Infer()` | 将 `atlas::utils::Tensor` 的裸指针封装为 `Ort::Value`（零拷贝），调用 `session_->Run()` |
| `GetInputInfo()` | 从 `session_` 读取输入节点名、shape、dtype，转换为 `TensorInfo` |
| `Unload()` | reset `session_` 和 `env_`，置 `loaded_ = false` |

**ONNX Runtime dtype → atlas DataType 映射：**

| ONNXTensorElementDataType | atlas::DataType |
|---------------------------|-----------------|
| `ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT` | `kFloat32` |
| `ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16` | `kFloat16` |
| `ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8` | `kInt8` |
| `ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8` | `kUInt8` |
| `ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32` | `kInt32` |

---

### 3.2 Pipeline 框架

**文件：** `src/pipeline/`

```
src/pipeline/
├── pipeline.h          # Pipeline 编排类
├── pipeline.cc
├── pipeline_node.h     # IPipelineNode 抽象接口
├── nodes/
│   ├── resize_node.h / .cc
│   ├── normalize_node.h / .cc
│   ├── hwc_to_chw_node.h / .cc
│   ├── bgr_to_rgb_node.h / .cc
│   └── dtype_convert_node.h / .cc
└── BUILD
```

**IPipelineNode 接口：**

```cpp
namespace atlas {
namespace pipeline {

class IPipelineNode {
 public:
    virtual ~IPipelineNode() = default;

    // Processes one tensor in-place or produces a new output tensor.
    // Input and output may point to the same Tensor only if the node
    // declares in-place support via SupportsInPlace().
    virtual utils::ErrorCode Process(const utils::Tensor& input,
                                      utils::Tensor* output) = 0;

    virtual std::string_view Name() const = 0;
    virtual bool SupportsInPlace() const { return false; }
};

}  // namespace pipeline
}  // namespace atlas
```

**Pipeline 编排类：**

```cpp
namespace atlas {
namespace pipeline {

class Pipeline {
 public:
    // Appends a processing node to the pipeline.
    void AddNode(std::unique_ptr<IPipelineNode> node);

    // Runs all nodes sequentially.  Input flows through each node in order.
    // |scratch| is used as intermediate buffer between nodes.
    utils::ErrorCode Run(const utils::Tensor& input,
                          utils::Tensor* output) const;

    // Builds a default input preprocessing pipeline from TensorInfo.
    // Inserts nodes in the order: DtypeConvert → Resize → BGRToRGB → HWCToCHW → Normalize.
    // Only nodes whose transformation is actually needed are added.
    static Pipeline BuildInputPipeline(const utils::TensorInfo& target_info);

    bool IsEmpty() const { return nodes_.empty(); }

 private:
    std::vector<std::unique_ptr<IPipelineNode>> nodes_;
};

}  // namespace pipeline
}  // namespace atlas
```

**内置节点规格：**

| 节点类 | 输入 | 输出 | 参数 |
|--------|------|------|------|
| `DtypeConvertNode` | 任意 dtype | 目标 dtype | `target_dtype` |
| `ResizeNode` | HWC uint8/float32 | 目标 H×W | `target_h, target_w`，双线性插值 |
| `BGRToRGBNode` | 3 通道 | 3 通道（通道翻转）| 无 |
| `HWCToCHWNode` | H×W×C | C×H×W | 无 |
| `NormalizeNode` | float32 | float32 | `mean[C], std[C]` 逐通道归一化 |

**Pipeline 自动构建逻辑（`BuildInputPipeline`）：**

```
TensorInfo.dtype != kUInt8  →  跳过 DtypeConvert
TensorInfo.shape[H] / [W]   →  插入 ResizeNode（目标尺寸从 shape 读取）
                （用户可通过 layout 判断 H/W 在 shape 中的位置）
TensorInfo.layout == "NCHW" →  插入 HWCToCHWNode（原始图像默认 HWC）
TensorInfo.has_normalize    →  插入 NormalizeNode
```

---

### 3.3 WORKSPACE 新增依赖

```python
# ONNX Runtime 1.17.3 prebuilt — macOS arm64
http_archive(
    name = "onnxruntime_macos_arm64",
    url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-osx-arm64-1.17.3.tgz",
    sha256 = "<run: shasum -a 256 onnxruntime-osx-arm64-1.17.3.tgz>",
    strip_prefix = "onnxruntime-osx-arm64-1.17.3",
    build_file = "//third_party:onnxruntime.BUILD",
)

# ONNX Runtime 1.17.3 prebuilt — Linux x86_64
http_archive(
    name = "onnxruntime_linux_x86_64",
    url = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-x86_64-1.17.3.tgz",
    sha256 = "<run: shasum -a 256 onnxruntime-linux-x86_64-1.17.3.tgz>",
    strip_prefix = "onnxruntime-linux-x86_64-1.17.3",
    build_file = "//third_party:onnxruntime.BUILD",
)
```

`third_party/onnxruntime.BUILD` 关键内容：

```python
cc_library(
    name = "onnxruntime",
    hdrs = glob(["include/onnxruntime/**/*.h"]),
    includes = ["include"],
    srcs = select({
        "@bazel_tools//src/conditions:darwin_arm64": ["lib/libonnxruntime.dylib"],
        "//conditions:default":                      ["lib/libonnxruntime.so"],
    }),
    visibility = ["//visibility:public"],
)
```

`src/backend/cpu/BUILD` 通过 `select()` 选择正确的预编译包：

```python
cc_library(
    name = "cpu_backend",
    srcs = ["cpu_backend.cc"],
    hdrs = ["cpu_backend.h"],
    deps = [
        "//src/backend/base:backend_factory",
        "//src/backend/base:i_backend",
        select({
            "@bazel_tools//src/conditions:darwin_arm64": "@onnxruntime_macos_arm64//:onnxruntime",
            "//conditions:default":                      "@onnxruntime_linux_x86_64//:onnxruntime",
        }),
    ],
    visibility = ["//visibility:public"],
    alwayslink = 1,  # Ensures ATLAS_REGISTER_BACKEND static initializer runs.
)
```

---

## 四、目录结构（阶段二新增部分）

```
atlas/
└── src/
    ├── backend/
    │   └── cpu/
    │       ├── cpu_backend.h
    │       ├── cpu_backend.cc
    │       └── BUILD
    └── pipeline/
        ├── pipeline_node.h
        ├── pipeline.h
        ├── pipeline.cc
        ├── nodes/
        │   ├── resize_node.h / .cc
        │   ├── normalize_node.h / .cc
        │   ├── hwc_to_chw_node.h / .cc
        │   ├── bgr_to_rgb_node.h / .cc
        │   └── dtype_convert_node.h / .cc
        └── BUILD
tests/
    ├── backend/
    │   └── cpu/
    │       ├── cpu_backend_test.cc   # 加载模型、推理、验证输出形状
    │       ├── test_data/            # 小型测试用 .onnx 模型（如 identity / add）
    │       └── BUILD
    └── pipeline/
        ├── pipeline_test.cc          # 各节点单独测试 + Pipeline 链路测试
        └── BUILD
```

---

## 五、端到端集成测试方案

测试场景：给定一个最小 ONNX 模型（如输入 [1,3,224,224] → 输出 [1,1000] 的 identity 模型），通过以下链路验证整体可运行：

```
manifest.json
      │
      ▼
ManifestParser::Parse()
      │
      ▼
BackendFactory::Create("cpu")
      │
      ▼
CpuBackend::Load(model_path, config)
      │
      ▼
Pipeline::BuildInputPipeline(input_info)
      │   (uint8 HWC 224×224×3  →  float32 NCHW 1×3×224×224)
      ▼
Pipeline::Run(raw_image, preprocessed_tensor)
      │
      ▼
CpuBackend::Infer(inputs, outputs)
      │
      ▼
验证 outputs[0].info.shape == [1, 1000]
```

---

## 六、阶段二不包含的内容

- ModelManager 与 ModelPool（阶段三）
- 对外 Atlas API 封装（阶段三）
- TensorRT / RKNN / SNPE 等硬件加速后端（阶段四）
- OpenCV 版 Resize（可选扩展，按需接入）
- 后处理管线节点（如 NMS、Softmax，随具体模型需求添加）

---

## Feature 记录

| Proposal | 日期 | 简述 | 状态 |
|----------|------|------|------|
| [Proposal-001](proposals/001-pipeline-manifest-config.md) | 2026-06-26 | Manifest 自由配置 Pipeline | 已采纳 |

---

## Bugfix 记录

| BUG | 日期 | 简述 | 等级 | 状态 |
|-----|------|------|------|------|
