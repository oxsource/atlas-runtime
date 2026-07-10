# SNPE Shared Buffer Example

演示两个 SNPE 模型通过 `SnpeMemoryPool` 共享同一块输入物理内存。

## 目录结构

```
examples/snpe_shared_buffer/
├── BUILD              # Bazel 构建配置
├── main.cc            # 主程序
├── manifest.json      # 双模型 manifest
├── gen_models.py      # DLC 测试模型生成脚本
└── README.md          # 本文件
```

## 前置条件

- 构建平台：`linux_aarch64` 或 `android_arm64`
- SNPE SDK（1.x 或 2.x）已安装并配置 `SNPE_SDK_PATH`
- 两个测试用 DLC 模型（`model_a.dlc` 和 `model_b.dlc`）

## 生成测试模型

```bash
python3 examples/snpe_shared_buffer/gen_models.py /tmp/snpe_models
```

如 SNPE 工具链不可用，脚本会输出 placeholder 提示。
实际环境中可以用 TensorFlow/ONNX 创建简单的 ReLU identity 模型后转换。

## 构建

```bash
bazel build //examples/snpe_shared_buffer:snpe_shared_buffer
```

## 运行

```bash
SAMPLE_MODEL_DIR=/tmp/snpe_models \
  ./bazel-bin/examples/snpe_shared_buffer/snpe_shared_buffer \
  examples/snpe_shared_buffer/manifest.json
```

**输出示例**（SNPE SDK 可用时）：

```
[snpe_shared_buffer] Atlas SNPE Shared Buffer Sample  (v0.x.y)
[snpe_shared_buffer] -- Step 1: Initialize Runtime --
[snpe_shared_buffer] Runtime initialized from: examples/snpe_shared_buffer/manifest.json
[snpe_shared_buffer] -- Step 2: Get Model Handles --
[snpe_shared_buffer] [model_a] input 'input'  shape: [1,3,4,4,]  dtype=0
[snpe_shared_buffer] [model_b] input 'input'  shape: [1,3,4,4,]  dtype=0
[snpe_shared_buffer] -- Step 3: Write Input via model_a --
[snpe_shared_buffer] Wrote 1x3x4x4 gradient to model_a buffer.
[snpe_shared_buffer] -- Step 4: Verify Shared Memory --
[snpe_shared_buffer] model_a buffer: 0x7f8a000000
[snpe_shared_buffer] model_b buffer: 0x7f8a000000
[snpe_shared_buffer] ✓ Shared buffer verified: both models use identical memory.
[snpe_shared_buffer] -- Step 5: Run Inference --
[snpe_shared_buffer] model_a inference OK, 1 output(s).
[snpe_shared_buffer] model_b inference OK, 1 output(s).
[snpe_shared_buffer] -- Step 6: Compare Outputs --
[snpe_shared_buffer] Output[0]: model_a 48 floats, model_b 48 floats
[snpe_shared_buffer]   ✓ Outputs match (shared input → identical results).
[snpe_shared_buffer] -- Step 7: Release --
[snpe_shared_buffer] Runtime released. Done.
```

## 原理

1. 两个模型在 manifest 中都配置了 `"shared_input": "camera_feed"`
2. `model_a` 加载时，`SnpeMemoryPool::AcquireShared("camera_feed", ...)` 分配对齐内存，refcount=1
3. `model_b` 加载时，`AcquireShared("camera_feed", ...)` 返回同一指针，refcount=2
4. 向 `model_a` 的 buffer 写入数据 → `model_b` 的 buffer 立即可见
5. 两个模型执行推理，结果应一致
6. `model_b` 卸载 → `ReleaseShared`，refcount=1
7. `model_a` 卸载 → `ReleaseShared`，refcount=0，内存回收到 free list
