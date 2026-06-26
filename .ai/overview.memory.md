# Atlas AI Memory — Overview

> Compiled from docs/ for AI Coding Agents. Source of truth is always docs/.

## Project

- Atlas: C++17 visual model runtime framework
- Goal: manage multiple models via a single manifest file, with unified API and pluggable backends
- Build system: Bazel 6.5, C++17
- Current version: 1.0.0 (all 6 phases implemented)

## Core Modules

- `src/api/` — Public API: `AtlasRuntime`, `ModelHandle` (namespace `atlas::api`)
- `src/core/` — `ManifestParser`, `ModelManager`, `ManifestConfig` (namespace `atlas::core`)
- `src/backend/base/` — `IBackend`, `IBackendContext`, `BackendFactory` (namespace `atlas::backend`)
- `src/backend/cpu/` — `CpuBackend`, `CpuBackendContext` (ONNX Runtime 1.17.3)
- `src/pipeline/` — `Pipeline`, `IPipelineNode`, `PipelineNodeFactory`, built-in nodes (namespace `atlas::pipeline`)
- `src/utils/` — `Tensor`, `TensorInfo`, `DataType`, `ErrorCode`, `VersionString` (namespace `atlas::utils`)
- `src/public/` — Public library bridge: `atlas_init.cc`, public headers in `include/atlas/`

## Development Phases (Fixed, No New Phases)

| Phase | Content | Status |
|-------|---------|--------|
| 1 | Bazel + ManifestParser + IBackend interface | Done |
| 2 | CpuBackend (ONNX Runtime) + Pipeline | Done |
| 3 | ModelManager + AtlasRuntime API + Tests | Done |
| 4 | TensorRT / RKNN / SNPE backends (not started) | Planned |
| 5 | Examples + Benchmarks + Release | Done |
| 6 | Public shared library + Bazel integration | Done |

> **No new phases allowed.** Features supplement existing phase docs only.

## Key Dependencies

- ONNX Runtime 1.17.3 (prebuilt: macOS arm64 + Linux x86_64)
- nlohmann/json 3.11.3 (header-only, manifest parsing)
- GoogleTest 1.12.1 (unit tests)

## AI Collaboration Rules

- Developer makes all design decisions; AI executes only
- AI must NOT modify code/docs without developer confirmation
- Documentation must exist before code implementation
- Every change must trace to a proposal or phase document
