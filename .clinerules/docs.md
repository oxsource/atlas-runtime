---
paths:
  - "docs/**"
  - "**/*.md"
---
# Documentation Rules

Full spec: `docs/doc_spec.md`

## Structure
```
docs/
├── *_spec.md          # Specs — no version header needed
├── phase{N}.md        # 6 fixed phase docs — version header required
├── phase{N}_result.md # Per-phase result (as needed)
├── proposals/NNN-*.md # Feature proposals
└── bugfixes/BUG-NNN-*.md # Bug reports
```

## Rules
- `phase{N}.md` requires version header: `文档版本 / 对应代码版本 / 最后更新 / 状态`
- Chapter numbers must be unique and consecutive within each doc
- Internal links: relative paths from `docs/`, e.g. `[Proposal-001](proposals/001-xxx.md)`
- Supplement sections: reference only — no design duplication
- Forbidden terms: ~~`type`~~ (use `name`), ~~`external_consumer`~~ (use `two_model_pipeline`)

## Doc Review Triggers (any one)
- 10 doc commits since last review
- 3+ proposals adopted
- Architecture change
- 30 days elapsed

Commit format: `docs: periodic doc review — YYYY-MM-DD`
