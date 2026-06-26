# Atlas AI Memory — Architecture

> Compiled from docs/architecture.md.

## Layered Architecture

```
Application → Atlas API (Init/GetModel/Run/Release)
                  ↓                    ↓
           ModelManager           Pipeline (preprocess/postprocess)
           (ManifestParser,
            ModelPool)
                  ↓
           Backend Abstraction (IBackend: Load/Infer/GetInputInfo/Unload)
                  ↓
     ONNX RT / TensorRT / SNPE / RKNN
```

## Data Flow

```
manifest.json → ManifestParser → ManifestConfig
→ ModelManager::Init (create BackendContext per type, build Pipelines)
→ AtlasRuntime::GetModel → ModelHandle
→ ModelHandle::Run (Pipeline preprocess → IBackend::Infer → Pipeline postprocess)
```

## Module Dependencies

- `api` → `core`, `utils`
- `core` → `backend/base`, `pipeline`, `utils`
- `backend/cpu` → `backend/base`, `core:manifest_config`, `utils`, `@onnxruntime`
- `pipeline` → `core:manifest_config`, `utils`, `pipeline_node_factory`
- `pipeline/nodes` → `pipeline_node`, `pipeline_node_factory`, `utils` (alwayslink=1)
- `public` → aggregates all above + `@onnxruntime`

## Directory Structure

```
src/api/          — AtlasRuntime, ModelHandle
src/core/         — ManifestParser, ModelManager, manifest_config
src/backend/base/ — IBackend, IBackendContext, BackendFactory
src/backend/cpu/  — CpuBackend, CpuBackendContext
src/pipeline/     — Pipeline, IPipelineNode, PipelineNodeFactory
src/pipeline/nodes/ — 9 built-in nodes + README.md
src/utils/        — types.h, version.h
src/public/       — Public lib bridge (atlas_init.cc, include/atlas/)
tests/            — Unit tests per module
examples/         — two_model_pipeline (uses public API only)
tools/            — install_atlas.sh, atlas.pc.in
third_party/      — BUILD files for external deps
```

## Backend Registration

- `ATLAS_REGISTER_BACKEND("cpu", CpuBackend)` — in `cpu_backend.cc`
- `ATLAS_REGISTER_BACKEND_CONTEXT("cpu", CpuBackendContext)` — in `cpu_backend_context.cc`
- Both targets use `alwayslink = 1` in BUILD
- `BackendFactory::Instance()` singleton, thread-safe registration

## Pipeline Node Registration

- `ATLAS_REGISTER_PIPELINE_NODE("name", ClassName)` — at end of each node's .cc
- `PipelineNodeFactory::Instance()` singleton
- Nodes target uses `alwayslink = 1` in BUILD
- Each node implements `static CreateFromParams(params) → unique_ptr<IPipelineNode>`

## Shared Context Pattern

- One `IBackendContext` per unique backend type string, shared across all instances
- `CpuBackendContext` holds single `Ort::Env`
- `ModelManager` creates contexts in `Init()`, passes to `IBackend::Load()`

## PIMPL Pattern

- `AtlasRuntime` uses `unique_ptr<ManifestParser>` and `unique_ptr<ModelManager>`
- Public header (`include/atlas/atlas_runtime.h`) has no internal includes
- Internal header (`src/api/atlas_runtime.h`) used by tests/examples directly
