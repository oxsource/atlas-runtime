# 阶段五实现方案：示例应用 + 性能基准测试 + 发布

## 一、目标与交付物

| 交付物 | 说明 |
|--------|------|
| `examples/two_model_pipeline` | 双模型组合推理示例，覆盖完整 Atlas API 使用路径 |
| `benchmarks/infer_benchmark` | 单模型推理延迟 / 吞吐量基准测试程序 |
| 版本标签与发布包 | `CHANGELOG`、版本头文件、打包脚本 |

## 二、示例应用目标

提供一个完整可运行的示例程序，演示如何通过 Atlas 公开 API 完成以下任务：

1. 通过一份清单文件配置两个独立的视觉模型；
2. 使用 `AtlasRuntime` / `ModelHandle` 进行两个模型的组合推理；
3. 覆盖 `Pipeline` 自动预处理、模型间结果传递、输出解析等完整流程。

---

## 二、示例场景：双阶段图像分析

以一个简化的"检测 + 分类"场景为例：

```
原始图像 (HWC uint8)
        │
        ▼
┌────────────────────────┐
│  Model 1: detector     │  ← Pipeline 自动完成 DtypeConvert / Resize /
│  输入: [1,3,32,32]     │    BGRToRGB / HWCToCHW / Normalize
│  输出: [1,3,32,32]     │
└────────────┬───────────┘
             │  detection features
             ▼
       ┌─────────────┐
       │  应用层逻辑  │  ← 从 detector 输出取得感兴趣区域分数，
       │  (main.cc)  │    打印 top-1 结果
       └─────────────┘
             │
             ▼
┌────────────────────────┐
│  Model 2: classifier   │  ← 同样的原始图像再次输入，
│  输入: [1,3,32,32]     │    两模型共享一个 CpuBackendContext
│  输出: [1,3,32,32]     │
└────────────────────────┘
```

> **说明**：示例使用小尺寸（32×32）的 Identity ONNX 模型以保持零额外依赖，
> 实际部署时替换为真实检测/分类模型即可，其余代码不变。

---

## 三、目录结构

```
atlas/
└── examples/
    └── two_model_pipeline/
        ├── BUILD                  # Bazel 构建目标
        ├── gen_models.py          # 生成两个示例 ONNX 模型
        ├── manifest.json          # 清单文件（路径通过环境变量配置）
        ├── main.cc                # 示例主程序
        └── README.md              # 运行说明
```

---

## 四、清单文件设计（manifest.json）

```json
{
  "version": "1.0",
  "name": "two-model-pipeline",
  "models": [
    {
      "id": "detector",
      "name": "Feature Detector",
      "backend": "cpu",
      "model_path": "${SAMPLE_MODEL_DIR}/detector.onnx",
      "load_strategy": "eager",
      "inputs": [
        {
          "name": "images",
          "shape": [1, 3, 32, 32],
          "dtype": "float32",
          "layout": "NCHW",
          "normalize": {
            "mean": [0.485, 0.456, 0.406],
            "std":  [0.229, 0.224, 0.225]
          }
        }
      ],
      "outputs": [
        {
          "name": "features",
          "shape": [1, 3, 32, 32],
          "dtype": "float32"
        }
      ],
      "config": { "num_threads": "2" }
    },
    {
      "id": "classifier",
      "name": "Score Classifier",
      "backend": "cpu",
      "model_path": "${SAMPLE_MODEL_DIR}/classifier.onnx",
      "load_strategy": "lazy",
      "inputs": [
        {
          "name": "images",
          "shape": [1, 3, 32, 32],
          "dtype": "float32",
          "layout": "NCHW"
        }
      ],
      "outputs": [
        {
          "name": "scores",
          "shape": [1, 3, 32, 32],
          "dtype": "float32"
        }
      ],
      "config": { "num_threads": "2" }
    }
  ]
}
```

**关键配置说明：**

| 字段 | 说明 |
|------|------|
| `${SAMPLE_MODEL_DIR}` | 运行时通过环境变量指定模型目录，与代码解耦 |
| `detector` 的 `load_strategy: eager` | 程序启动时立即加载，确保首次推理无延迟 |
| `classifier` 的 `load_strategy: lazy` | 首次调用 `GetModel` 时才加载，演示懒加载策略 |
| 两个模型同为 `"backend": "cpu"` | `ModelManager` 只创建一份 `CpuBackendContext`（`Ort::Env`），共享使用 |
| `detector` 配置了 `normalize` | `Pipeline::BuildInputPipeline()` 自动插入 `NormalizeNode` |
| `classifier` 无 `normalize` | 管线仅做类型转换 + 布局转换 |

---

## 五、示例主程序（main.cc）

```cpp
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "src/api/atlas_runtime.h"
#include "src/api/model_handle.h"
#include "src/utils/types.h"

// Include backend registrations via alwayslink deps.
#include "src/backend/cpu/cpu_backend.h"
#include "src/backend/cpu/cpu_backend_context.h"

namespace {

// -----------------------------------------------------------------------
// Build a synthetic 32×32 BGR raw image (HWC uint8).
// In a real application this would be decoded from a camera or file.
// -----------------------------------------------------------------------
atlas::utils::Tensor CreateSampleBGRImage(int h, int w) {
    atlas::utils::Tensor img;
    img.info.dtype  = atlas::utils::DataType::kUInt8;
    img.info.shape  = {h, w, 3};
    img.info.layout = "HWC";
    img.byte_size   = static_cast<size_t>(h * w * 3);
    img.data        = malloc(img.byte_size);
    img.owns_data   = true;

    // Gradient pattern: B channel increases with row, G with col, R = 128.
    uint8_t* p = static_cast<uint8_t*>(img.data);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            p[(r * w + c) * 3 + 0] = static_cast<uint8_t>(r * (255 / h));
            p[(r * w + c) * 3 + 1] = static_cast<uint8_t>(c * (255 / w));
            p[(r * w + c) * 3 + 2] = 128;
        }
    }
    return img;
}

// -----------------------------------------------------------------------
// Find the index with the maximum absolute value in a float buffer.
// Used as a stand-in for "top-1 class" in a real classification model.
// -----------------------------------------------------------------------
int ArgMaxAbs(const float* data, size_t count) {
    int   best  = 0;
    float best_v = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float v = data[i] < 0 ? -data[i] : data[i];
        if (v > best_v) { best_v = v; best = static_cast<int>(i); }
    }
    return best;
}

}  // namespace

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: two_model_pipeline <manifest_path>\n";
        return 1;
    }
    const std::string manifest_path = argv[1];

    // ── Step 1: Initialize AtlasRuntime ──────────────────────────────────
    // Parses the manifest, creates one CpuBackendContext (shared Ort::Env),
    // and eagerly loads "detector".
    atlas::api::AtlasRuntime runtime;
    auto ret = runtime.Init(manifest_path);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Init failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }
    std::cout << "[OK] Runtime initialized.\n";

    // ── Step 2: Obtain model handles ─────────────────────────────────────
    // "classifier" is lazy — its backend is loaded here on first GetModel.
    auto detector   = runtime.GetModel("detector");
    auto classifier = runtime.GetModel("classifier");

    if (!detector.IsValid() || !classifier.IsValid()) {
        std::cerr << "[ERROR] Failed to obtain model handles.\n";
        return 1;
    }

    // Print model I/O metadata exposed via Pipeline + Backend.
    const auto det_inputs = detector.GetInputInfo();
    std::cout << "[detector] input: " << det_inputs[0].name
              << "  shape: [";
    for (int d : det_inputs[0].shape) std::cout << d << ",";
    std::cout << "]\n";

    // ── Step 3: Prepare raw input image ──────────────────────────────────
    // The raw image is HWC uint8 BGR — Atlas Pipeline handles the rest.
    constexpr int kH = 32, kW = 32;
    auto raw_image = CreateSampleBGRImage(kH, kW);
    std::cout << "[OK] Created " << kH << "x" << kW
              << " BGR image (HWC uint8).\n";

    // ── Step 4: Run detector ──────────────────────────────────────────────
    // Pipeline automatically executes:
    //   DtypeConvert(uint8→float32)
    //   → Resize(32×32)  [no-op here since source is already 32×32]
    //   → BGRToRGB
    //   → HWCToCHW
    //   → Normalize(ImageNet mean/std)
    // Then calls CpuBackend::Infer().
    std::vector<atlas::utils::Tensor> det_outputs;
    ret = detector.Run(raw_image, &det_outputs);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Detector inference failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }
    std::cout << "[detector] inference OK. output count: "
              << det_outputs.size() << "\n";

    // ── Step 5: Application-level result processing ───────────────────────
    const float* det_data  = static_cast<const float*>(det_outputs[0].data);
    const size_t det_count = det_outputs[0].byte_size / sizeof(float);
    int   det_top1 = ArgMaxAbs(det_data, det_count);
    float det_val  = det_data[det_top1];
    std::cout << "[detector] top-1 activation: index=" << det_top1
              << "  value=" << det_val << "\n";

    // ── Step 6: Run classifier (same raw image, independent pipeline) ─────
    std::vector<atlas::utils::Tensor> cls_outputs;
    ret = classifier.Run(raw_image, &cls_outputs);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Classifier inference failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }
    std::cout << "[classifier] inference OK. output count: "
              << cls_outputs.size() << "\n";

    const float* cls_data  = static_cast<const float*>(cls_outputs[0].data);
    const size_t cls_count = cls_outputs[0].byte_size / sizeof(float);
    int   cls_top1 = ArgMaxAbs(cls_data, cls_count);
    float cls_val  = cls_data[cls_top1];
    std::cout << "[classifier] top-1 score: index=" << cls_top1
              << "  value=" << cls_val << "\n";

    // ── Step 7: Combined result ───────────────────────────────────────────
    std::cout << "\n[RESULT] detector_top1=" << det_top1
              << "  classifier_top1=" << cls_top1 << "\n";

    // ── Step 8: Release all resources ────────────────────────────────────
    runtime.Release();
    std::cout << "[OK] Runtime released.\n";
    return 0;
}
```

---

## 六、模型生成脚本（gen_models.py）

使用 `onnx` 库生成两个最小 Identity ONNX 模型，供示例直接运行：

```python
"""Generates detector.onnx and classifier.onnx for the two_model_pipeline sample."""

import os
import sys
import onnx
from onnx import TensorProto, helper


def make_identity_model(model_id: str, input_name: str,
                         output_name: str, shape: list,
                         out_dir: str) -> None:
    node = helper.make_node("Identity",
                             inputs=[input_name],
                             outputs=[output_name])
    input_vi  = helper.make_tensor_value_info(input_name,  TensorProto.FLOAT, shape)
    output_vi = helper.make_tensor_value_info(output_name, TensorProto.FLOAT, shape)
    graph  = helper.make_graph([node], f"{model_id}_graph", [input_vi], [output_vi])
    model  = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 11)],
        ir_version=7)
    path = os.path.join(out_dir, f"{model_id}.onnx")
    onnx.save(model, path)
    print(f"Saved: {path}")


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/atlas_sample_models"
    os.makedirs(out_dir, exist_ok=True)
    # detector: 1×3×32×32 identity
    make_identity_model("detector",   "images",  "features", [1, 3, 32, 32], out_dir)
    # classifier: 1×3×32×32 identity (no normalize in manifest, simpler pipeline)
    make_identity_model("classifier", "images",  "scores",   [1, 3, 32, 32], out_dir)
```

---

## 七、Bazel BUILD 配置

```python
# examples/two_model_pipeline/BUILD

cc_binary(
    name = "two_model_pipeline",
    srcs = ["main.cc"],
    data = ["manifest.json"],
    deps = [
        "//src/api:atlas_runtime",
        "//src/api:model_handle",
        "//src/backend/cpu:cpu_backend",
        "//src/backend/cpu:cpu_backend_context",
        "//src/utils:types",
    ],
)

# Convenience: generate sample models at build time (requires Python + onnx).
genrule(
    name = "gen_sample_models",
    srcs = ["gen_models.py"],
    outs = [
        "models/detector.onnx",
        "models/classifier.onnx",
    ],
    cmd = "$(location @python//:python) $(location gen_models.py) $$(dirname $(location models/detector.onnx))",
    tools = ["@python//:python"],
)
```

---

## 八、运行步骤

```bash
# 1. 生成示例 ONNX 模型
python3 examples/two_model_pipeline/gen_models.py /tmp/atlas_sample_models

# 2. 设置模型目录环境变量（清单中 ${SAMPLE_MODEL_DIR} 的展开值）
export SAMPLE_MODEL_DIR=/tmp/atlas_sample_models

# 3. 构建示例程序
bazel build //examples/two_model_pipeline:two_model_pipeline

# 4. 运行
./bazel-bin/examples/two_model_pipeline/two_model_pipeline \
    examples/two_model_pipeline/manifest.json
```

**预期输出：**

```
[OK] Runtime initialized.
[detector] input: images  shape: [1,3,32,32,]
[OK] Created 32x32 BGR image (HWC uint8).
[detector] inference OK. output count: 1
[detector] top-1 activation: index=<N>  value=<V>
[classifier] inference OK. output count: 1
[classifier] top-1 score: index=<N>  value=<V>

[RESULT] detector_top1=<N>  classifier_top1=<N>
[OK] Runtime released.
```

---

## 九、Pipeline 行为说明

下图展示 `detector` 模型的完整数据流（`classifier` 同理，无 Normalize 节点）：

```
raw_image (HWC uint8, 32×32×3)
        │
        ▼ DtypeConvertNode (uint8 → float32, cast only)
float32 HWC, 32×32×3,  values ∈ [0, 255]
        │
        ▼ ResizeNode (目标 H=32, W=32 → 本例无缩放)
float32 HWC, 32×32×3
        │
        ▼ BGRToRGBNode (channel 0 ↔ channel 2)
float32 HWC, 32×32×3, RGB order
        │
        ▼ HWCToCHWNode ([H,W,C] → [C,H,W])
float32 CHW, 3×32×32
        │
        ▼ ModelHandle::Run() 插入 batch dim → [1,3,32,32]
        │
        ▼ NormalizeNode  (x/255 − mean) / std   ← detector 专属
float32 NCHW, [1,3,32,32],  values ∈ [-2.1, 2.6]
        │
        ▼ CpuBackend::Infer()
output: float32 NCHW [1,3,32,32]
```

> `classifier` 不含 `normalize`，数据在 HWCToCHW 后直接进入推理，值域仍为 [0, 255]。

---

## 十、与现有模块的关系

| Atlas 模块 | 示例中的体现 |
|-----------|------------|
| `ManifestParser` | `AtlasRuntime::Init()` 内部解析 `manifest.json` |
| `ModelManager` | 按类型创建一份 `CpuBackendContext`，管理两个 `ModelEntry` |
| `IBackendContext` / `CpuBackendContext` | 两个模型共享同一 `Ort::Env` |
| `Pipeline::BuildInputPipeline()` | 从清单 inputs[0] 自动构建预处理节点链 |
| `ModelHandle::Run()` | 封装 Pipeline + Infer 完整链路，对外一行调用 |
| `ErrorCode` | 所有调用结果通过返回值检查，无异常 |

---

## 十一、性能基准测试方案

### 11.1 目标

| 指标 | 说明 |
|------|------|
| 单次推理延迟（latency） | `Run()` 调用的 P50 / P95 / P99，单位 ms |
| 吞吐量（throughput） | 固定时间窗口内完成的推理次数（infer/sec） |
| 冷启动时间 | `AtlasRuntime::Init()` 耗时（首次加载模型） |
| 内存占用 | RSS 峰值（可选，通过 `/proc/self/status` 或 macOS API 读取） |

### 11.2 目录结构

```
atlas/
└── benchmarks/
    └── infer_benchmark/
        ├── BUILD
        ├── benchmark_main.cc   # 基准测试主程序
        ├── gen_bench_model.py  # 生成指定尺寸的 Identity 模型
        └── manifest.json       # 基准测试专用清单
```

### 11.3 基准程序设计（benchmark_main.cc）

```cpp
// Usage:
//   infer_benchmark <manifest_path> <model_id> <warmup> <iterations>
//
// Example:
//   infer_benchmark bench_manifest.json detector 10 1000

#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>

#include "src/api/atlas_runtime.h"
#include "src/api/model_handle.h"
#include "src/utils/types.h"

// Collects latencies for |iterations| calls to handle.Run(image, &outputs).
// Returns latency vector in microseconds.
std::vector<double> MeasureLatencies(atlas::api::ModelHandle& handle,
                                      const atlas::utils::Tensor& image,
                                      int warmup, int iterations) {
    std::vector<atlas::utils::Tensor> outputs;

    // Warm-up: allow ORT to JIT-compile kernels.
    for (int i = 0; i < warmup; ++i) {
        outputs.clear();
        handle.Run(image, &outputs);
    }

    std::vector<double> latencies;
    latencies.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        outputs.clear();
        auto t0 = std::chrono::high_resolution_clock::now();
        handle.Run(image, &outputs);
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return latencies;
}

// Prints P50/P95/P99 from a sorted latency vector (µs → ms).
void PrintPercentiles(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return v[static_cast<size_t>(p * v.size())]; };
    std::cout << "  P50  = " << pct(0.50) / 1000.0 << " ms\n";
    std::cout << "  P95  = " << pct(0.95) / 1000.0 << " ms\n";
    std::cout << "  P99  = " << pct(0.99) / 1000.0 << " ms\n";
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    std::cout << "  Mean = " << sum / v.size() / 1000.0 << " ms\n";
    std::cout << "  Throughput ≈ "
              << 1e6 / (sum / v.size()) << " infer/sec\n";
}
```

### 11.4 基准测试清单（bench_manifest.json）

```json
{
  "version": "1.0",
  "name": "benchmark",
  "models": [
    {
      "id": "detector",
      "backend": "cpu",
      "model_path": "${BENCH_MODEL_DIR}/detector_224.onnx",
      "load_strategy": "eager",
      "inputs": [
        {
          "name": "images",
          "shape": [1, 3, 224, 224],
          "dtype": "float32",
          "layout": "NCHW",
          "normalize": {
            "mean": [0.485, 0.456, 0.406],
            "std":  [0.229, 0.224, 0.225]
          }
        }
      ],
      "outputs": [{"name": "out", "shape": [1, 3, 224, 224], "dtype": "float32"}],
      "config": {"num_threads": "4"}
    }
  ]
}
```

---

## 十二、发布准备

### 12.1 版本头文件（`src/utils/version.h`）

```cpp
#pragma once

namespace atlas {
namespace utils {

constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 0;
constexpr int kVersionPatch = 0;

// Returns "1.0.0"
const char* VersionString();

}  // namespace utils
}  // namespace atlas
```

### 12.2 CHANGELOG

遵循 [Keep a Changelog](https://keepachangelog.com) 格式维护 `CHANGELOG.md`：

```markdown
## [1.0.0] - YYYY-MM-DD
### Added
- ManifestParser: JSON manifest parsing with env-var expansion
- CpuBackend: ONNX Runtime 1.17.3 CPU inference
- Pipeline: DtypeConvert / Resize / BGRToRGB / HWCToCHW / Normalize nodes
- ModelManager: eager/lazy loading, shared BackendContext per type
- AtlasRuntime / ModelHandle: unified public API
- examples/two_model_pipeline: dual-model inference demo
- benchmarks/infer_benchmark: latency / throughput measurement tool
```

### 12.3 发布 Checklist

| 项目 | 完成条件 |
|------|---------|
| 所有单元测试通过 | `bazel test //...` 全绿 |
| 示例程序可正常运行 | `two_model_pipeline` 输出预期结果 |
| 基准测试有参考数据 | 至少记录 macOS arm64 下 224×224 的延迟基线 |
| `CHANGELOG.md` 更新 | 包含本版本所有新增/修改 |
| 版本头文件 | `kVersionMajor/Minor/Patch` 与 git tag 一致 |
| `README.md` 补充 | 快速上手步骤（克隆 → 生成模型 → 运行示例） |
| git tag 打标 | `git tag v1.0.0 && git push origin v1.0.0` |

---

## 十三、阶段五不包含的内容

- TensorRT / RKNN / SNPE 后端（阶段四单独接入）
- Python / C 语言绑定接口
- 多线程并发推理（当前 `ModelHandle::Run()` 为单线程）
- 模型加密 / 鉴权机制
