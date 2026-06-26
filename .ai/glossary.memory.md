# Atlas AI Memory — Glossary

> Terms and conventions for AI agents working on Atlas.

## Project Terms

| Term | Meaning |
|------|---------|
| Atlas | The project name — visual model runtime framework |
| Manifest | JSON file describing all models and their configs |
| Backend | Inference engine implementation (ONNX RT, TensorRT, etc.) |
| Pipeline | Chain of preprocessing/postprocessing nodes |
| ModelHandle | Lightweight non-owning reference to a loaded model |
| AtlasRuntime | Top-level facade, manages lifecycle |
| ModelManager | Internal manager, owns backends and pipelines |
| BackendFactory | Singleton registry for backend creation |
| PipelineNodeFactory | Singleton registry for pipeline node creation |
| alwayslink | Bazel attribute ensuring static initializers are retained |

## Namespace Map

| Namespace | Module |
|-----------|--------|
| `atlas::api` | AtlasRuntime, ModelHandle |
| `atlas::core` | ManifestParser, ModelManager, ManifestConfig, ModelConfig |
| `atlas::backend` | IBackend, IBackendContext, BackendFactory, CpuBackend |
| `atlas::pipeline` | Pipeline, IPipelineNode, PipelineNodeFactory, nodes |
| `atlas::utils` | Tensor, TensorInfo, DataType, ErrorCode, VersionString |
| `atlas::public_api` | EnsureBackendsLinked (anchor function) |

## File Conventions

| Path Pattern | Content |
|---------------|---------|
| `docs/architecture.md` | Architecture overview, phase table (source of truth) |
| `docs/code_spec.md` | Coding standards |
| `docs/feature_spec.md` | Feature/proposal management rules |
| `docs/bugfix_spec.md` | Bug report and fix management rules |
| `docs/phase_spec.md` | Phase development workflow + commit rules |
| `docs/doc_spec.md` | Document management + periodic review rules |
| `docs/phase{N}.md` | Phase design docs (fixed 6, version headers required) |
| `docs/proposals/NNN-*.md` | Feature proposals (incremental, never recycled) |
| `docs/bugfixes/BUG-NNN-*.md` | Bug reports (incremental, independent from proposals) |
| `src/public/include/atlas/` | Public API headers (no internal includes) |
| `src/pipeline/nodes/README.md` | Built-in node reference (exported via public BUILD data) |
| `.ai/*.memory.md` | AI Memory (this directory, compiled from docs/) |

## Version Conventions

- Code version: `1.0.0` (kVersionMajor.Minor.Patch in version.h)
- Doc version in phase{N}.md: `X.Y.Z`
  - Patch+1: supplementary content (TODO, notes,局限性)
  - Minor+1: design changes (interface signatures, data structure changes)
  - No increment: typo/format fixes
- Manifest version: `"1.0"` (major.minor, major must match parser's supported major)

## Status Vocabulary

### Phase doc status
- `设计` (draft) → `评审中` (under review) → `已实现` (implemented)

### Proposal status
- `草案` → `讨论中` → `已采纳` / `已驳回`

### Bug status
- `待确认` → `已确认` → `修复中` → `已修复` / `已驳回`

### Bug severity
- P0 (fatal) / P1 (severe) / P2 (general) / P3 (minor)
