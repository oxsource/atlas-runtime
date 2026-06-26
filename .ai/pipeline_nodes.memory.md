# Atlas AI Memory — Pipeline Nodes

> Compiled from src/pipeline/nodes/README.md and source code.

## Built-in Node Registry

| name | Class | Params | Direction | InPlace |
|------|-------|--------|-----------|---------|
| `dtype_convert` | DtypeConvertNode | `target` (string, required) | preprocess | no |
| `resize` | ResizeNode | `height`, `width` (int, required) | preprocess | no |
| `bgr_to_rgb` | BGRToRGBNode | none | preprocess | yes |
| `rgb_to_bgr` | RGBToBGRNode | none | preprocess | yes |
| `hwc_to_chw` | HWCToCHWNode | none | preprocess | no |
| `chw_to_hwc` | CHWToHWCNode | none | preprocess/postprocess | no |
| `normalize` | NormalizeNode | `mean`, `std` (comma-separated floats, required) | preprocess | yes |
| `softmax` | SoftmaxNode | `axis` (int, optional, default -1) | postprocess | yes |
| `topk` | TopKNode | `k` (int, required) | postprocess | no |

## Node Interface Contract

```cpp
class IPipelineNode {
    virtual ErrorCode Process(const Tensor& input, Tensor* output) = 0;
    virtual string_view Name() const = 0;
    virtual bool SupportsInPlace() const { return false; }
};
```

## Registration Pattern

```cpp
// At end of each node's .cc file:
ATLAS_REGISTER_PIPELINE_NODE("name", ClassName)

// Each node must implement:
static unique_ptr<IPipelineNode> CreateFromParams(
    const unordered_map<string, string>& params);
```

## Auto-Build Pipeline (when `pipeline` field absent)

Fixed order, nodes added only when needed:
1. DtypeConvert (uint8→float32, always)
2. Resize (if NCHW 4-D shape, H>0 W>0)
3. BGRToRGB (if NCHW and C==3)
4. HWCToCHW (if layout=="NCHW")
5. Normalize (if has_normalize && mean/std non-empty)

## Params Parsing

- Integer params: stored as `std::to_string(v.get<int64_t>())` → "224"
- Float params: stored as `std::to_string(v.get<double>())` → "0.485000"
- String params: stored as-is
- Boolean params: stored as "true" / "false"
- Node's `CreateFromParams` parses strings back to typed values (std::stoi, std::stof, etc.)

## Pipeline Execution

- Ping-pong buffer pattern between nodes
- Empty pipeline = identity (output points to input, no copy)
- `Pipeline::Run()` stops on first node error
