# GitHub Copilot Guidance — Atlas Project

> This file provides context to GitHub Copilot when assisting with the Atlas project.
> Last updated: 2026-06-26

## Project

Atlas is a C++17 visual model runtime framework (Bazel 6.5). It manages multiple ONNX models via a single JSON manifest, with a unified C++ API and pluggable backend architecture.

## AI Memory — Read First

Compiled AI Memory files live in `.ai/`. Read them before generating code suggestions:

| File | When to Read |
|------|-------------|
| [`.ai/overview.memory.md`](.ai/overview.memory.md) | Always — project goal, modules, phases |
| [`.ai/architecture.memory.md`](.ai/architecture.memory.md) | Module dependencies, data flow, registration patterns |
| [`.ai/coding_convention.memory.md`](.ai/coding_convention.memory.md) | Any code generation — naming, namespaces, rules |
| [`.ai/api_contract.memory.md`](.ai/api_contract.memory.md) | API signatures, types, manifest JSON format |
| [`.ai/rules_decisions.memory.md`](.ai/rules_decisions.memory.md) | Commits, feature/bugfix flow, architecture decisions |
| [`.ai/pipeline_nodes.memory.md`](.ai/pipeline_nodes.memory.md) | Pipeline node work, manifest pipeline config |
| [`.ai/glossary.memory.md`](.ai/glossary.memory.md) | Term definitions, file conventions, version rules |

> `.ai/` files are compressed from `docs/`. For full context, refer to `docs/`.

## Must-Follow Rules

- **C++17, Google Style Guide** — `#pragma once`, snake_case files, PascalCase classes, ≥2-layer namespaces (`atlas::<module>::`)
- **No exceptions** — return `atlas::utils::ErrorCode` from all public APIs
- **No magic values** — use `constexpr` named constants
- **All comments in English**
- **No bare `new`/`delete`** — use `std::unique_ptr`; `Tensor` is move-only
- **Pipeline node config field is `name`**, not `type`
- **Fixed 6 phases** — no new phases; features supplement existing `docs/phase{N}.md`
- **Commits need `AI-Tool` trailer** — e.g., `AI-Tool: GitHub Copilot / GPT-4`

## Build & Test

```bash
bazel build //...
bazel test //...
```

## Source Map

```
src/api/           AtlasRuntime, ModelHandle
src/core/          ManifestParser, ModelManager, manifest_config.h
src/backend/base/  IBackend, BackendFactory
src/backend/cpu/   CpuBackend (ONNX Runtime 1.17.3)
src/pipeline/      Pipeline, PipelineNodeFactory, IPipelineNode
src/pipeline/nodes/ 9 built-in nodes
src/utils/         types.h (Tensor, ErrorCode, DataType), version.h
src/public/        Public lib + include/atlas/ headers
tests/             8 test suites
examples/          two_model_pipeline
```

## Copilot-Specific Tips

- When suggesting completions, match the existing include order: `.h` → C stdlib → C++ stdlib → third-party → project headers
- Use `alwayslink = 1` in BUILD for any target containing `ATLAS_REGISTER_*` macros
- Backend and pipeline node registrations use anonymous namespace + `__COUNTER__` for unique variable names
- `ManifestTensorInfo` (internal) vs `TensorInfo` (public) — don't mix them; use `.ToTensorInfo()` to convert
- Manifest `pipeline` field is optional — absent means auto-build (fixed node order)
