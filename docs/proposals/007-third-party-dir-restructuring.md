# Proposal-007: 三方库目录按类型分文件夹管理

> **提议日期**：2026-06-30
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：模块级
> **关联**：`atlas_deps.bzl`、`third_party/`、`docs/phase3.md`

---

## 一、背景

当前 `third_party/` 下所有三方库的 BUILD 文件均平铺在同一目录中：

```
third_party/
├── BUILD
├── nlohmann_json.BUILD
├── onnxruntime.BUILD
├── onnxruntime_android_arm64.BUILD
└── onnxruntime_android_x86_64.BUILD
```

随着后端和平台扩展，同一类型三方库的跨平台变体文件数量持续增长，ONNX Runtime 已有 3 个变体（桌面端通用 + Android arm64 + Android x86_64），SNPE、QNN 等移动端后端也将引入各自的 ABI 变体。平铺模式存在以下问题：

1. **可发现性差**：无法直观看出哪些 BUILD 文件属于同一三方库
2. **命名膨胀**：文件名只能靠前缀 + 后缀拼凑差异，`onnxruntime_android_arm64.BUILD` 形式冗长
3. **扩展性低**：新增变体时平铺目录持续膨胀，无内聚分类

### 1.1 通用型 vs 平台型三方库

| 类型 | 特征 | 示例 |
|------|------|------|
| 通用型 | 单一 BUILD，跨平台使用 | `nlohmann_json`（纯头文件） |
| 平台型 | 同一库有多个平台/ABI 变体 | `onnxruntime`（5 个平台） |

平台型三方库是本次重构的重点。

---

## 二、方案概要

**核心规则：一个三方库类型 = 一个子目录，该库的所有平台/ABI 变体 BUILD 文件统一收纳在内。**

```
third_party/
├── BUILD                                  # 包声明（不变）
├── nlohmann_json/                         # 通用型：纯头文件
│   └── nlohmann_json.BUILD
└── onnxruntime/                           # 平台型：预编译库变体
    ├── onnxruntime.BUILD                  # 桌面端（macOS arm64 + Linux x86_64 + Linux aarch64）
    ├── onnxruntime_android_arm64.BUILD    # Android arm64-v8a
    └── onnxruntime_android_x86_64.BUILD   # Android x86_64
```

之后新增 SNPE 后端时：

```
third_party/
├── BUILD
├── nlohmann_json/
│   └── nlohmann_json.BUILD
├── onnxruntime/
│   ├── onnxruntime.BUILD
│   ├── onnxruntime_android_arm64.BUILD
│   └── onnxruntime_android_x86_64.BUILD
└── snpe/                                  # 新库：SNPE SDK
    ├── snpe.BUILD                         # Android arm64-v8a
    └── snpe_x86_64.BUILD                  # Android x86_64（模拟器）
```

---

## 三、详细设计

### 3.1 文件移动清单

所有 BUILD 文件内容保持不变，仅变更路径：

| 原路径 | 新路径 |
|--------|--------|
| `third_party/nlohmann_json.BUILD` | `third_party/nlohmann_json/nlohmann_json.BUILD` |
| `third_party/onnxruntime.BUILD` | `third_party/onnxruntime/onnxruntime.BUILD` |
| `third_party/onnxruntime_android_arm64.BUILD` | `third_party/onnxruntime/onnxruntime_android_arm64.BUILD` |
| `third_party/onnxruntime_android_x86_64.BUILD` | `third_party/onnxruntime/onnxruntime_android_x86_64.BUILD` |

### 3.2 `atlas_deps.bzl` 路径更新

`http_archive` 的 `build_file` 参数从包级别 Label 更新为子目录级别 Label（共 7 处）：

| 位置 | 原 Label | 新 Label |
|------|----------|----------|
| L31 nlohmann_json | `@atlas//third_party:nlohmann_json.BUILD` | `@atlas//third_party/nlohmann_json:nlohmann_json.BUILD` |
| L40 onnxruntime_macos_arm64 | `@atlas//third_party:onnxruntime.BUILD` | `@atlas//third_party/onnxruntime:onnxruntime.BUILD` |
| L49 onnxruntime_linux_x86_64 | `@atlas//third_party:onnxruntime.BUILD` | `@atlas//third_party/onnxruntime:onnxruntime.BUILD` |
| L58 onnxruntime_linux_aarch64 | `@atlas//third_party:onnxruntime.BUILD` | `@atlas//third_party/onnxruntime:onnxruntime.BUILD` |
| L70 onnxruntime_android_arm64 | `@atlas//third_party:onnxruntime_android_arm64.BUILD` | `@atlas//third_party/onnxruntime:onnxruntime_android_arm64.BUILD` |
| L78 onnxruntime_android_x86_64 | `@atlas//third_party:onnxruntime_android_x86_64.BUILD` | `@atlas//third_party/onnxruntime:onnxruntime_android_x86_64.BUILD` |

### 3.3 `third_party/BUILD` 包声明

现有 `third_party/BUILD` 仅注释声明「No cc_library targets」，无需修改。Bazel 会自动识别子目录中的 BUILD 文件。

---

## 四、受影响文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `third_party/nlohmann_json.BUILD` | 移动 | → `third_party/nlohmann_json/nlohmann_json.BUILD` |
| `third_party/onnxruntime.BUILD` | 移动 | → `third_party/onnxruntime/onnxruntime.BUILD` |
| `third_party/onnxruntime_android_arm64.BUILD` | 移动 | → `third_party/onnxruntime/onnxruntime_android_arm64.BUILD` |
| `third_party/onnxruntime_android_x86_64.BUILD` | 移动 | → `third_party/onnxruntime/onnxruntime_android_x86_64.BUILD` |
| `third_party/BUILD` | 不变 | 包声明文件 |
| `atlas_deps.bzl` | 修改 | 7 处 `build_file` Label 指向新路径 |
| `docs/proposals/002-snpe-backend.md` | 修改 | 更新 `third_party/snpe.BUILD` 为 `third_party/snpe/snpe.BUILD` |

### 4.1 不解改的文件

以下文件中引用的 `third_party/` 路径属于**历史文档记录**（phase1.md、phase2.md、proposal-003、proposal-004），描述的是创建时的路径状态。根据 `feature_spec.md` 第 4.5 节，阶段文档的历史内容**不改写**，仅在对应 `phase{N}.md` 的 Feature 记录表格中追加本提案的补充标注。

---

## 五、不包含的内容

- BUILD 文件内部逻辑修改（如 `cc_library` 的 `srcs`/`hdrs` 调整）
- `select()` 分支变更
- `.ai/` 记忆文件更新（由 AI 工具自行维护）

---

## 六、后续动作

- [ ] 评审讨论
- [ ] 阶段归属：`→ phase3.md`（补充：Feature 记录）
- [ ] 实现：
  - [ ] 创建 `third_party/nlohmann_json/` 和 `third_party/onnxruntime/` 子目录
  - [ ] 移动 4 个 BUILD 文件到对应子目录
  - [ ] 更新 `atlas_deps.bzl` 中 7 处 `build_file` Label
  - [ ] 更新 `docs/proposals/002-snpe-backend.md` 中 `third_party/snpe.BUILD` 路径引用为 `third_party/snpe/snpe.BUILD`
- [ ] `bazel build //...` + `bazel test //...` 通过
- [ ] 实现完成后更新本文档状态为「已采纳」并归档