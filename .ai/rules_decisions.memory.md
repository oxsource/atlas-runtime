# Atlas AI Memory — Rules & Decisions

> Compiled from docs/architecture.md, feature_spec.md, bugfix_spec.md, phase_spec.md, doc_spec.md.

## Hard Rules

### Phase Framework
- Exactly 6 phases, FIXED. No new phases ever.
- Features can only supplement existing phase docs, never create new `phase{N}.md`.
- Phase docs: phase1.md ~ phase6.md (phase4 not yet started).

### Feature Flow
- All non-trivial changes require a proposal in `docs/proposals/NNN-*.md` before coding.
- Proposal states: 草案 → 讨论中 → 已采纳 / 已驳回.
- Adopted proposals supplement to existing `phase{N}.md` Feature record table (link format).
- Proposal numbering: 3-digit, incremental, never recycled.
- 5-business-day review window; no objection = auto-adopt (unless disputed).

### Bugfix Flow
- All bugs require `docs/bugfixes/BUG-NNN-*.md` before fixing.
- Bug states: 待确认 → 已确认 → 修复中 → 已修复 / 已驳回.
- Severity: P0 (fatal, immediate) / P1 (severe, current iteration) / P2 (general, next) / P3 (minor).
- P0/P1 must have regression tests.
- Bug numbering: incremental, independent from proposals.
- Commit format: `fix: BUG-NNN — <description>`

### Commit Convention
- Feature: `feat: phase N — <description>` or `feat: Proposal-NNN — <description>`
- Bugfix: `fix: BUG-NNN — <description>`
- Doc review: `docs: periodic doc review — <date>`
- Body: `-` bullet list of changes
- Trailer: `AI-Tool: <tool> / <model>` (required for all commits since spec生效)
- Early commits (phase 1-5) without trailer: no backfill needed.

### Document Rules
- `phase{N}.md` must have version header (文档版本/对应代码版本/最后更新/状态).
- Other docs (specs, architecture) do NOT need version headers.
- Supplementary sections in phase docs: reference only (background + proposal link), no detailed design duplication.
- Feature/Bugfix record tables use Markdown links: `[Proposal-001](proposals/001-xxx.md)`.
- Section numbers must be unique and consecutive within each document.
- Internal links use relative paths from `docs/`.

### Doc Review Triggers
- 10 doc commits since last review
- 3+ adopted proposals
- Architecture change
- 30 days since last review
- Commit format: `docs: periodic doc review — <date>`

### Terminology
- Pipeline node config field: `name` (NOT ~~`type`~~)
- Example program: `examples/two_model_pipeline` (NOT ~~`external_consumer`~~)
- Feature action: "supplement to existing phase" (NOT ~~"create new phase"~~)

## Architecture Decisions (ADR)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Language | C++17 | Performance, cross-platform, inference lib compat |
| Manifest format | JSON (nlohmann/json) | Header-only, mature, no external runtime |
| Backend registration | Factory + alwayslink + compile-time select | Platform-specific trimming |
| Tensor memory | Unified Tensor, move-only, zero-copy | Avoid redundant copies |
| Error handling | ErrorCode return values, no exceptions | Embedded/edge friendly |
| Build system | Bazel 6.5 | Reproducible, multi-platform, dependency isolation |
| Public API isolation | PIMPL pattern | Hide internal types from public headers |
| Pipeline config | Declarative manifest `pipeline` field + PipelineNodeFactory | Flexible node order, backward compatible |
| Manifest/public type separation | ManifestTensorInfo vs TensorInfo | Prevent manifest-specific fields in public API |

## Build Configuration

- `.bazelrc`: `--cxxopt=-std=c++17`, `--host_cxxopt=-std=c++17`
- Platform select: `@bazel_tools//src/conditions:darwin_arm64` vs `//conditions:default`
- ONNX Runtime: `@onnxruntime_macos_arm64` / `@onnxruntime_linux_x86_64`
- Public lib: `//src/public:atlas` (copts: -fvisibility=hidden, -DATLAS_SHARED_LIBRARY, alwayslink=1)
- Build/test: `bazel build //...` / `bazel test //...`
