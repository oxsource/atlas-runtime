# Atlas AI Memory — Coding Convention

> Compiled from docs/code_spec.md.

## Language & Style

- C++17, Google C++ Style Guide
- `#pragma once` only (no `#ifndef` guards)
- All comments in English (including Doxygen)
- `cpplint --recursive src/` before commit
- `.clang-format`: BasedOnStyle=Google, IndentWidth=4, ColumnLimit=100

## Naming

| Element | Rule | Example |
|---------|------|---------|
| Directories | lowercase + underscore | `src/backend/base/` |
| Files | snake_case | `manifest_parser.cc` |
| Test files | `<module>_test.cc` | `pipeline_test.cc` |
| Namespaces | ≥2 layers | `atlas::core::` |
| Classes | PascalCase | `ManifestParser` |
| Interfaces | I-prefix | `IBackend` |
| Data structs | struct, PascalCase | `TensorInfo` |
| Functions | PascalCase | `GetInputInfo()` |
| Locals | snake_case | `model_path` |
| Members | snake_case_ (trailing) | `is_loaded_` |
| Static members | s_snake_case_ | `s_instance_` |
| Constants | kPascalCase | `kMaxModels` |
| Enums | kPascalCase | `kFloat32` |
| Macros | ATLAS_ALL_CAPS | `ATLAS_REGISTER_BACKEND` |

## Namespace Layers

- `atlas::` — top-level
- `atlas::core::` — manifest, model manager
- `atlas::backend::` — backend abstraction + implementations
- `atlas::pipeline::` — pipeline + nodes
- `atlas::api::` — public API
- `atlas::utils::` — types, helpers

## Rules

- No `using namespace` in headers
- `using` only in function body in .cc files
- `} // namespace xxx` comment required after namespace close
- No C++ exceptions (`throw`/`catch` banned)
- All public APIs return `utils::ErrorCode`
- No `std::cerr` in library code
- No magic values — use `constexpr` named constants
- `unique_ptr` for ownership; `shared_ptr` only when truly shared
- No bare `new`/`delete` in business logic (only in `Tensor` internals)
- `Tensor` is move-only (copy deleted)

## Include Order (in .cc)

1. Corresponding .h
2. C stdlib (`<cstdint>`)
3. C++ stdlib (`<string>`, `<vector>`)
4. Third-party (`<nlohmann/json.hpp>`)
5. Project headers (`"src/utils/types.h"`)

## Bazel

- One BUILD per directory, describes only that directory's targets
- Default visibility `//visibility:private`; explicit `public` when needed
- Third-party deps managed in `third_party/`, not inline `http_archive`
- Test targets: `<module>_test` in `tests/<module>/`
- `alwayslink = 1` for backend and node registration targets
