---
paths:
  - "docs/proposals/**"
---
# Proposals

Full spec: `docs/feature_spec.md`

## Rules
- Non-trivial changes require a proposal **before** coding
- File: `docs/proposals/NNN-short-kebab-title.md` (3-digit, incremental, never recycled)
- States: 草案 → 讨论中 → 已采纳 / 已驳回
- Review window: 5 business days; no objection = auto-adopt

## Template Header
```markdown
# Proposal-NNN: <Title>
> **提议日期**: YYYY-MM-DD  **提议人**: name  **状态**: 草案
> **类型**: 阶段级 / 模块级 / 小型  **关联**: <phase doc or 无>
```

## After Adoption
1. Add row to `phase{N}.md` Feature 记录 table: `[Proposal-NNN](proposals/NNN-xxx.md)`
2. Add supplement section to `phase{N}.md` (reference only — no design duplication):
   `> **【补充】** Proposal-NNN | date | 详细设计见 docs/proposals/NNN-xxx.md`
