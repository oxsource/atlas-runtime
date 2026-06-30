---
paths:
  - "tests/**"
  - "**/*_test.cc"
---
# Testing

## Commands
```bash
bazel build //...
bazel test //...   # All 8 suites must pass before any commit
```

## Test Suites
`//tests/core:manifest_parser_test` · `//tests/core:model_manager_test` · `//tests/pipeline:pipeline_test` · `//tests/pipeline:pipeline_manifest_test` · `//tests/api:atlas_runtime_test` · `//tests/public:atlas_export_test` · `//tests/public:atlas_lib_test` · `//tests/backend/cpu:...`

## Requirements
- All 8 suites pass before any commit
- P0/P1 bugs **must** include a regression test
- New public API method: ≥1 success path + ≥1 error path test
- New pipeline node: test valid params, missing params, output shape/dtype
- New backend: test Load/Infer/Unload with a minimal ONNX model
- Test data in `tests/<module>/test_data/`; no large binaries without approval

## Conventions
- Fixture: `class FooTest : public ::testing::Test`
- Use `ASSERT_EQ` for `ErrorCode` checks when subsequent steps depend on result
- Format: `TEST_F(FixtureName, ShouldBehaviorWhenCondition)`
- Tests may include internal headers directly (e.g. `src/api/atlas_runtime.h`)
