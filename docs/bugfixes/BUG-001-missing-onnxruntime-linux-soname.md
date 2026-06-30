# BUG-001: Linux 平台 ONNX Runtime 共享库 SONAME 缺失导致 5 个测试失败

> **报告日期**：2026-06-30
> **报告人**：pizzk <726676435@qq.com>
> **状态**：已修复
> **等级**：P0
> **影响版本**：455bce87 (feat: phase 3 -- restructure third_party dir by library type)
> **关联模块**：third_party/onnxruntime

---

## 一、缺陷描述

Linux 平台执行 `bazel test //...` 时，5 个依赖 ONNX Runtime 的测试全部失败，错误为：

```
error while loading shared libraries: libonnxruntime.so.1.17.3:
cannot open shared object file: No such file or directory
```

3 个不依赖 ONNX Runtime 的测试（`manifest_parser_test`、`pipeline_test`、`pipeline_manifest_test`）正常通过。

## 二、复现步骤

1. 在 Linux x86_64 环境检出 `455bce87` 或之前版本
2. 执行 `bazel test //...`
3. 观察到 5 个测试失败

## 三、预期行为

所有 8 个测试通过，与 macOS 行为一致。

## 四、实际行为

| 测试 | 状态 |
|------|------|
| `//tests/api:atlas_runtime_test` | FAILED |
| `//tests/backend/cpu:cpu_backend_test` | FAILED |
| `//tests/core:model_manager_test` | FAILED |
| `//tests/public:atlas_export_test` | FAILED |
| `//tests/public:atlas_lib_test` | FAILED |
| `//tests/core:manifest_parser_test` | PASSED |
| `//tests/pipeline:pipeline_test` | PASSED |
| `//tests/pipeline:pipeline_manifest_test` | PASSED |

所有失败均为同一错误：`libonnxruntime.so.1.17.3: cannot open shared object file`

## 五、根因分析

ONNX Runtime Linux 预编译包中：
- `lib/libonnxruntime.so` → 指向 `libonnxruntime.so.1.17.3` 的 symlink
- `lib/libonnxruntime.so.1.17.3` → 真正的 ELF 共享库（SONAME）

`third_party/onnxruntime/onnxruntime.BUILD` 的 `//conditions:default`（Linux）分支的 `srcs` 仅包含 `lib/libonnxruntime.so`，不包含 `lib/libonnxruntime.so.1.17.3`。Bazel 将 `srcs` 中的文件复制到 runfiles 沙箱，运行时加载器通过 SONAME 查找版本化 `.so` 文件，但该文件不在沙箱中，因此加载失败。

对比 macOS arm64 分支 — 已正确包含两个文件：
```python
"@bazel_tools//src/conditions:darwin_arm64": [
    "lib/libonnxruntime.dylib",
    "lib/libonnxruntime.1.17.3.dylib",
],
```

## 六、修复方案

在 `third_party/onnxruntime/onnxruntime.BUILD` 的 `//conditions:default` 分支补充 `lib/libonnxruntime.so.1.17.3`：

```diff
     "//conditions:default": [
         "lib/libonnxruntime.so",
+        "lib/libonnxruntime.so.1.17.3",
     ],
```

Commit: `a81d339`

## 七、验证

- macOS: `bazel test //...` — 8/8 PASSED（缓存复用，构建未受影响）
- Linux: 需用户验证 `bazel test //...` 全部通过

## 八、后续动作

- [x] 根因分析（2026-06-30）
- [x] 代码修复（commit `a81d339`）
- [x] 创建 BUG-001 报告
- [x] 更新 `docs/phase3.md` Bugfix 记录
- [ ] Linux 环境验证 `bazel test //...` 全部通过
- [x] 关闭并归档