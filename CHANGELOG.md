# Changelog

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [1.0.0] - 2026-06-26

### Added

#### Core
- `ManifestParser`: JSON manifest parsing with environment-variable expansion,
  version compatibility check, and duplicate-id detection.
- `ManifestConfig` / `ModelConfig`: structured manifest representation with
  `LoadStrategy` (eager / lazy) field.
- `IBackendContext`: per-backend-type shared runtime resource abstraction.
- `BackendFactory`: singleton registry for backend creators and context creators,
  with `ATLAS_REGISTER_BACKEND` / `ATLAS_REGISTER_BACKEND_CONTEXT` macros.
- `ModelManager`: manages one `IBackendContext` per unique backend type; supports
  eager and lazy model loading; thread-safe.

#### Backend
- `IBackend`: abstract inference interface (`Load` / `Infer` / `GetInputInfo` /
  `GetOutputInfo` / `Unload` / `IsLoaded`). `Load()` accepts an optional
  `IBackendContext*` for shared runtime resources.
- `CpuBackend`: ONNX Runtime 1.17.3 CPU inference backend. Borrows `Ort::Env`
  from `CpuBackendContext`; falls back to private env when no context provided.
- `CpuBackendContext`: holds a single `Ort::Env` shared across all cpu-backend
  model instances within one `ModelManager`.

#### Pipeline
- `IPipelineNode`: abstract preprocessing step interface.
- `Pipeline`: node chain with ping-pong buffer execution;
  `BuildInputPipeline()` auto-constructs from `TensorInfo`.
- Built-in nodes: `DtypeConvertNode`, `ResizeNode` (bilinear),
  `BGRToRGBNode`, `HWCToCHWNode`, `NormalizeNode` (per-channel, /255 scaling).

#### API
- `ModelHandle`: lightweight non-owning handle; `Run()` = Pipeline + Infer.
- `AtlasRuntime`: public facade — `Init` / `GetModel` / `Release` lifecycle.

#### Utilities
- `DataType`, `ErrorCode`, `Tensor`, `TensorInfo` common types.
- `ElementByteSize()` / `ElementCount()` helpers.
- `VersionString()` — returns `"1.0.0"`.

#### Examples
- `examples/two_model_pipeline`: dual-model (detector + classifier) end-to-end
  inference demo covering the complete Atlas API surface.

#### Benchmarks
- `benchmarks/infer_benchmark`: single-model latency / throughput benchmark
  with P50 / P95 / P99 / mean reporting and configurable warmup / iterations.

### Build
- Bazel 6.5 workspace with `nlohmann/json` 3.11.3 and `GoogleTest` 1.12.1.
- ONNX Runtime 1.17.3 prebuilt for macOS arm64 and Linux x86_64.
- Platform selection via `select()` in cpu backend BUILD targets.
- `.bazelrc` enforces C++17 for all targets.

### Tests
- 37 unit tests across 5 suites:
  `manifest_parser_test`, `cpu_backend_test`, `pipeline_test`,
  `model_manager_test`, `atlas_runtime_test`.

---

## Baseline Performance (macOS arm64, 4 threads, Identity model)

| Input size | P50 | P95 | P99 | Throughput |
|------------|-----|-----|-----|-----------|
| 224×224×3  | ~1.9 ms | ~2.1 ms | ~2.1 ms | ~511 infer/sec |

> These numbers reflect the Atlas framework overhead (Pipeline + ORT session
> dispatch) on an identity model, not real model compute time.
