# Commits

## Format
```
<type>: <scope> -- <description>

- change 1
- change 2

AI-Tool: Cline / <model>
```

## Types
| type | scope | example |
|------|-------|---------|
| `feat` | `phase N` or `Proposal-NNN` | `feat: phase 3 -- add lazy load` |
| `fix` | `BUG-NNN` | `fix: BUG-001 -- correct output shape` |
| `docs` | description | `docs: periodic doc review -- 2026-06-30` |
| `test` | module | `test: pipeline -- normalize edge cases` |
| `refactor` | module | `refactor: core -- extract validation` |

## Rules
- `AI-Tool` trailer required on all AI-assisted commits
- **Model name**: Cline must read `.clinerules/ai_model` to get the correct `<model>` value for the trailer. Do not guess or hardcode the model name.
- **Commit messages must be written in English using only ASCII characters** -- subject, body bullets, and trailer
- Do NOT use `--no-verify` or `--force` on shared branches
- One commit per concern: no mixing feat + fix

## Pre-commit Checklist
- [ ] `cpplint --recursive src/` clean
- [ ] `bazel build //...` passes
- [ ] `bazel test //...` all 8 suites pass
- [ ] Proposal or bug report exists (for non-trivial changes)
- [ ] `phase{N}.md` records updated if applicable
