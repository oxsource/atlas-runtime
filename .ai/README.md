# Atlas AI Memory

> Compiled from `docs/` for AI Coding Agents (Claude, Copilot, GPT, GLM, Qwen, Cursor, etc.).
> These files are NOT the source of truth — always refer to `docs/` for full details.
> Do NOT add new design decisions here; only extract and compress existing docs.

## Files

| File | Source | Content |
|------|--------|---------|
| `overview.memory.md` | architecture.md | Project goal, modules, phases, dependencies, AI collaboration rules |
| `architecture.memory.md` | architecture.md | Layered architecture, data flow, module deps, registration patterns, PIMPL |
| `coding_convention.memory.md` | code_spec.md | Naming, namespaces, rules, Bazel, include order |
| `api_contract.memory.md` | src/api, src/utils, src/core, src/pipeline, src/backend | All public/internal API signatures, types, manifest format |
| `rules_decisions.memory.md` | feature_spec, bugfix_spec, phase_spec, doc_spec, architecture | Hard rules, feature/bugfix flow, commit convention, ADRs |
| `pipeline_nodes.memory.md` | nodes/README.md, source | Node registry, params, auto-build logic, execution model |
| `glossary.memory.md` | all docs | Terms, namespace map, file conventions, version/status vocabulary |

## Maintenance

- Update when `docs/` changes significantly (follow doc_spec.md review triggers).
- Compression target: 5-15% of source docs, preserving AI-actionable knowledge only.
- Keep model-agnostic; no tool-specific instructions.
