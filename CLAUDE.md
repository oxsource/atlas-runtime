# CLAUDE.md

> This file provides guidance to Claude (and other AI coding agents) when working with the Atlas project.
> Last updated: 2026-06-26

## Project Overview

Atlas is a C++17 visual model runtime framework built with Bazel 6.5. It manages multiple inference models via a single JSON manifest file, with a unified API and pluggable backend architecture (ONNX Runtime CPU backend implemented; TensorRT/RKNN/SNPE planned).

## AI Memory — Read First

Before making any changes, read the compiled AI Memory files in `.ai/`. These contain high-density, structured knowledge extracted from the full documentation:

| File | Read When |
|------|-----------|
| [`.ai/overview.memory.md`](.ai/overview.memory.md) | Starting any work — project goals, modules, phases, AI collaboration rules |
| [`.ai/architecture.memory.md`](.ai/architecture.memory.md) | Touching module boundaries, dependencies, registration patterns |
| [`.ai/coding_convention.memory.md`](.ai/coding_convention.memory.md) | Writing or modifying any code — naming, namespaces, rules, Bazel |
| [`.ai/api_contract.memory.md`](.ai/api_contract.memory.md) | Touching public/internal APIs, types, manifest format |
| [`.ai/rules_decisions.memory.md`](.ai/rules_decisions.memory.md) | Starting a feature/bugfix, writing commits, making architecture decisions |
| [`.ai/pipeline_nodes.memory.md`](.ai/pipeline_nodes.memory.md) | Working on pipeline nodes, manifest pipeline config |
| [`.ai/glossary.memory.md`](.ai/glossary.memory.md) | Need term definitions, namespace map, file conventions |

> The `.ai/` files are **not** the source of truth. For full details, always refer to `docs/`.

## Critical Rules

1. **No new phases.** The project has exactly 6 phases (fixed). Features supplement existing `phase{N}.md` only.
2. **Documentation first.** Code implementation requires a corresponding design doc (`proposals/` or `phase{N}.md`) before coding.
3. **Developer leads, AI executes.** Do not modify code or finalized docs without developer confirmation. If you spot an issue, propose it — don't fix it unprompted.
4. **No exceptions.** All public APIs return `atlas::utils::ErrorCode`. No `throw`/`catch`.
5. **No magic values.** Use `constexpr` named constants for all meaningful literals.
6. **All comments in English.** Including Doxygen, inline, and block comments.
7. **Commits must include `AI-Tool` trailer.** Format: `AI-Tool: <tool> / <model>`. Commit messages must use only ASCII characters — no Chinese, emoji, or non-ASCII symbols.
8. **Pipeline node config field is `name`**, not ~~`type`~~.

## Build & Test

```bash
bazel build //...          # Build everything
bazel test //...           # Run all tests (8 test suites)
bazel build //src/public:atlas   # Build public shared library
```

## Key Source Locations

```
src/api/           → AtlasRuntime, ModelHandle (public API)
src/core/          → ManifestParser, ModelManager, manifest_config
src/backend/base/  → IBackend, BackendFactory
src/backend/cpu/   → CpuBackend, CpuBackendContext
src/pipeline/      → Pipeline, PipelineNodeFactory, IPipelineNode
src/pipeline/nodes/→ 9 built-in nodes + README.md
src/utils/         → types.h (Tensor, ErrorCode, DataType), version.h
src/public/        → Public lib bridge + include/atlas/ headers
tests/             → Unit tests (8 suites, all passing)
examples/          → two_model_pipeline (uses public API only)
```

## Development Workflow

```
docs/proposals/NNN-*.md  (proposal, developer reviews)
  → adopted → supplement to existing phase{N}.md Feature record
  → implement code + tests
  → bazel test //... passes
  → git commit with AI-Tool trailer
```

See [`.ai/rules_decisions.memory.md`](.ai/rules_decisions.memory.md) for full feature/bugfix/commit rules.
