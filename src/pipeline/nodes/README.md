# Atlas Pipeline 内置节点参考

> 本文档描述 Atlas Pipeline 中可用的内置节点及其参数规格。
> 在 manifest 的 `inputs[].pipeline` 和 `outputs[].pipeline` 中通过 `name` 字段引用这些节点。

## 节点清单

| name | 说明 | 参数 | 方向 |
|------|------|------|------|
| `dtype_convert` | 数据类型转换 | `target` | 预处理 |
| `resize` | 双线性缩放 | `height`, `width` | 预处理 |
| `bgr_to_rgb` | BGR → RGB 通道交换 | 无 | 预处理 |
| `rgb_to_bgr` | RGB → BGR 通道交换 | 无 | 预处理 |
| `hwc_to_chw` | HWC → CHW 布局转换 | 无 | 预处理 |
| `chw_to_hwc` | CHW → HWC 布局转换 | 无 | 预处理 / 后处理 |
| `normalize` | 逐通道归一化 | `mean`, `std` | 预处理 |
| `softmax` | Softmax 后处理 | `axis`（可选） | 后处理 |
| `topk` | Top-K 后处理 | `k` | 后处理 |

## 各节点详细说明

### dtype_convert
- 输入: 任意 dtype
- 输出: 目标 dtype
- 参数:
  - `target` (string, 必填): 目标数据类型，可选值 `float32` / `uint8` / `int8` / `int32`
- 示例: `{ "name": "dtype_convert", "params": { "target": "float32" } }`

### resize
- 输入: HWC uint8 / float32
- 输出: HWC 目标尺寸
- 参数:
  - `height` (int, 必填): 目标高度
  - `width` (int, 必填): 目标宽度
- 示例: `{ "name": "resize", "params": { "height": 224, "width": 224 } }`

### bgr_to_rgb
- 输入: HWC 3 通道 (BGR 顺序)
- 输出: HWC 3 通道 (RGB 顺序)
- 参数: 无
- 示例: `{ "name": "bgr_to_rgb" }`

### rgb_to_bgr
- 输入: HWC 3 通道 (RGB 顺序)
- 输出: HWC 3 通道 (BGR 顺序)
- 参数: 无
- 示例: `{ "name": "rgb_to_bgr" }`

### hwc_to_chw
- 输入: HWC (H × W × C)
- 输出: CHW (C × H × W)
- 参数: 无
- 示例: `{ "name": "hwc_to_chw" }`

### chw_to_hwc
- 输入: CHW (C × H × W)
- 输出: HWC (H × W × C)
- 参数: 无
- 示例: `{ "name": "chw_to_hwc" }`

### normalize
- 输入: float32 CHW
- 输出: float32 CHW (归一化后)
- 参数:
  - `mean` (逗号分隔的 float, 必填): 逐通道均值，如 `0.485,0.456,0.406`
  - `std` (逗号分隔的 float, 必填): 逐通道标准差，如 `0.229,0.224,0.225`
- 公式: `output = (input / 255 - mean[c]) / std[c]`
- 示例: `{ "name": "normalize", "params": { "mean": "0.485,0.456,0.406", "std": "0.229,0.224,0.225" } }`

### softmax
- 输入: float32 (1-D 或 multi-D)
- 输出: float32 (同形状，归一化为概率分布)
- 参数:
  - `axis` (int, 可选, 默认 -1): 计算 softmax 的轴
- 示例: `{ "name": "softmax" }` 或 `{ "name": "softmax", "params": { "axis": "1" } }`

### topk
- 输入: float32 1-D `[N]`
- 输出: float32 1-D `[k]` (前 k 个最大值，降序排列)
- 参数:
  - `k` (int, 必填): 返回的 top-k 数量
- 示例: `{ "name": "topk", "params": { "k": "5" } }`
