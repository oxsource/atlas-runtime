---
paths:
  - "docs/bugfixes/**"
---
# Bugfix

Full spec: `docs/bugfix_spec.md`

## Severity
| Level | Meaning | Regression Test |
|-------|---------|----------------|
| P0 | Fatal / data corruption | Required |
| P1 | Core feature broken | Required |
| P2 | Non-core, workaround exists | Recommended |
| P3 | Cosmetic / docs | Optional |

## Flow
1. Create `docs/bugfixes/BUG-NNN-short-kebab-title.md` (incremental, never recycled)
2. Developer confirms severity
3. Fix code + add regression test (P0/P1 required)
4. `bazel test //...` passes
5. `git commit: fix: BUG-NNN — <description>`
6. Add row to `phase{N}.md` Bugfix 记录 table

## Template Header
```markdown
# BUG-NNN: <Title>
> **报告日期**: YYYY-MM-DD  **状态**: 待确认  **等级**: P?
> **影响版本**: <hash>  **关联模块**: <src/...>
```
