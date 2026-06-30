# Development Workflow

## AI Collaboration Rules
- Developer makes all design decisions; AI executes only
- Do NOT modify code or docs without developer confirmation
- Propose issues — don't fix them unprompted
- Documentation must exist before code implementation

## Phase Constraint
Exactly **6 phases**, fixed. Never create `phase{N}.md`. Features only supplement existing phase docs.

## Standard Flow
```
docs/proposals/NNN-*.md  →  developer review  →  adopted
  →  supplement phase{N}.md Feature record
  →  implement code + tests
  →  bazel test //... passes
  →  git commit (with AI-Tool trailer)
```

See `docs/feature_spec.md`, `docs/bugfix_spec.md`, `docs/phase_spec.md` for full details.
