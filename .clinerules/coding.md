# Coding Standards

## Language
- C++17, Google Style. `#pragma once`. Indent=4, ColumnLimit=100.
- `cpplint --recursive src/` before commit. All comments in English.

## Naming
- Files: `snake_case.cc/.h` | Test files: `<module>_test.cc`
- Namespace: ≥2 layers `atlas::<module>` | Classes: `PascalCase` | Interfaces: `IPrefix`
- Backend namespace: each backend type gets a dedicated third layer: `atlas::backend::<type>` (e.g. `atlas::backend::snpe`, `atlas::backend::cpu`). This prevents inline function name collisions between different backends.
- Functions: `PascalCase` | Locals: `snake_case` | Members: `snake_case_` | Statics: `s_snake_case_`
- Constants/Enums: `kPascalCase` | Macros: `ATLAS_ALL_CAPS`
- `} // namespace xxx` required after every namespace close
- No `using namespace` in headers; `using` only in `.cc` function bodies

## Include Order (.cc)
1. Corresponding `.h` → 2. C stdlib → 3. C++ stdlib → 4. Third-party → 5. Project headers

## Hard Rules
- **No exceptions**: all public APIs return `atlas::utils::ErrorCode`
- **No magic values**: `constexpr` named constants only
- **No bare `new`/`delete`**: use `unique_ptr`; `shared_ptr` only when truly shared
- **`Tensor` is move-only**: copy deleted — never attempt to copy
- **No `std::cerr`** in library code
- `ManifestTensorInfo` (internal) ≠ `TensorInfo` (public); use `.ToTensorInfo()` to convert
- Pipeline node config field: `name` NOT ~~`type`~~

## Bazel
- One `BUILD` per directory; `//visibility:private` by default
- `alwayslink = 1` for any target with `ATLAS_REGISTER_*` macros
- Third-party deps in `third_party/` only
