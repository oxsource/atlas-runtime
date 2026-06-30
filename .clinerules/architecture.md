# Architecture

## Data Flow
`manifest.json` → `ManifestParser` → `ModelManager::Init()` → `AtlasRuntime::GetModel()` → `ModelHandle::Run()` → Pipeline preprocess → `IBackend::Infer()` → Pipeline postprocess

## Module Map
| Directory | Namespace | Key Types |
|-----------|-----------|-----------|
| `src/api/` | `atlas::api` | `AtlasRuntime`, `ModelHandle` |
| `src/core/` | `atlas::core` | `ManifestParser`, `ModelManager` |
| `src/backend/base/` | `atlas::backend` | `IBackend`, `IBackendContext`, `BackendFactory` |
| `src/backend/cpu/` | `atlas::backend` | `CpuBackend`, `CpuBackendContext` |
| `src/pipeline/` | `atlas::pipeline` | `Pipeline`, `IPipelineNode`, `PipelineNodeFactory` |
| `src/pipeline/nodes/` | `atlas::pipeline` | 9 built-in nodes |
| `src/utils/` | `atlas::utils` | `Tensor`, `TensorInfo`, `DataType`, `ErrorCode` |
| `src/public/` | — | Public lib bridge + `include/atlas/` headers |

## Key Design Decisions
- PIMPL on `AtlasRuntime`: public header has no internal includes
- One `IBackendContext` per backend type, shared across all model instances
- `ManifestTensorInfo` (internal) vs `TensorInfo` (public) — never mix; use `.ToTensorInfo()`
- Pipeline `pipeline` field absent = auto-build (fixed node order)
- Exactly **6 phases**, fixed — features only supplement existing `phase{N}.md`

## Registration
```cpp
// backend: alwayslink=1 required
ATLAS_REGISTER_BACKEND("cpu", CpuBackend)
ATLAS_REGISTER_BACKEND_CONTEXT("cpu", CpuBackendContext)

// pipeline node: alwayslink=1 required; implement CreateFromParams()
ATLAS_REGISTER_PIPELINE_NODE("bgr_to_rgb", BGRToRGBNode)
```

## Built-in Pipeline Nodes
`dtype_convert`(target) · `resize`(height,width) · `bgr_to_rgb` · `rgb_to_bgr` · `hwc_to_chw` · `chw_to_hwc` · `normalize`(mean,std) · `softmax`([axis]) · `topk`(k)

## Public API
See `src/api/atlas_runtime.h` and `src/api/model_handle.h`.
ErrorCodes: `kOk` `kInvalidArgument` `kFileNotFound` `kParseError` `kVersionMismatch` `kBackendNotFound` `kInferFailed` `kNotInitialized`
