# Atlas 阶段开发规范（phase_spec）

本文档总结前两个阶段的经验，形成可复用的阶段开发流程，供后续阶段参考。

---

## 一、阶段开发标准流程

```
proposals/（提议池）
        │
        │  评审采纳（详见 feature_spec.md）
        ▼
[Step 0] 提议评审采纳，补充到已有 phase{N}.md
        │
        ▼
[Step 1] 形成阶段设计文档（phase{N}.md 记录章节引用）
        │
        ▼
[Step 2] 开发者评估文档，确认方案
        │
        ▼
[Step 3] 实现代码 + 单元测试
        │
        ▼
[Step 4] 构建验证，所有测试通过
        │
        ▼
[Step 5] 编写阶段结果文档（phase{N}_result.md）
        │
        ▼
[Step 6] git commit
```

> **Step 0 说明：** 在正式编写方案前，先通过 `docs/proposals/` 提议池完成想法收集与初步评审。提议流程、模板与状态流转详见 **`docs/feature_spec.md`**。**注意：项目阶段划分为固定框架（阶段一至六），不再新增阶段。** 提议采纳后只能补充到已有的 `phase{N}.md` 中（按 `feature_spec.md` 4.5 节补充标注规范执行）。小型特性经提议采纳后可直接进入 Step 3 实现，跳过 Step 1/2。

---

## 二、各步骤说明与 AI 提示话术

### Step 1 — 形成阶段设计文档

**目的：** 将 `architecture.md` 中某一阶段的简要描述展开为可执行的具体方案。

**文档路径：** `docs/phase{N}.md`

**文档应包含：**
- **版本头**（置于标题下方首行，格式见下方说明）
- 目标与交付物清单
- 三方库选型（含引入方式、sha256 获取方法）
- 核心数据结构定义
- 关键接口设计（类/方法签名）
- 目录结构规划（仅本阶段新增部分）
- 单元测试覆盖点
- 明确标注本阶段**不包含**的内容（划定边界）

**版本头格式：**

```markdown
# 阶段N实现方案：xxx

> **文档版本**：1.0.0
> **对应代码版本**：v1.0.0（git tag）
> **最后更新**：YYYY-MM-DD
> **状态**：设计 / 评审中 / 已实现
```

| 字段 | 说明 |
|------|------|
| 文档版本 | 本篇文档的语义化版本，方案有实质变更时递增 |
| 对应代码版本 | 文档描述的实现所对应的 git tag（未实现时填 `—`） |
| 最后更新 | 最近一次修订日期 |
| 状态 | `设计`（初稿）→ `评审中`（提交讨论）→ `已实现`（代码合并后） |

**补充章节规范：**

当 feature 提议（`docs/proposals/NNN-*.md`）经评审采纳后，需补充到已有 `phase{N}.md` 时，**阶段文档中仅保留对提议的引用**，详细方案由提议文档自身承载。阶段文档补充格式如下：

```markdown
## 补充章节：xxx

> **【补充】** Proposal-NNN | YYYY-MM-DD | 文档版本 X.Y.Z → X.Y.W
> 以下内容由 Proposal-NNN 评审采纳后追加。

<简要描述补充的背景与关联的局限性，1-2 句即可。>

**详细设计方案见 `docs/proposals/NNN-xxx.md`。**
```

> **原则**：阶段文档的补充章节只做引用与背景说明，不重复提议文档中的详细设计（代码结构、接口签名、清单示例等）。这避免两份文档内容重复导致维护时产生不一致。

**Feature 与 Bugfix 记录章节：**

每个 `phase{N}.md` 在定稿时应包含以下两个固定章节，用于记录本阶段实现后追加的 feature 提议与 bugfix 记录。章节在阶段初始创建时为空，后续随采纳的提议 / 缺陷报告逐步填充：

```markdown
## Feature 记录

| Proposal | 日期 | 简述 | 状态 |
|----------|------|------|------|
| [Proposal-001](proposals/001-pipeline-manifest-config.md) | 2026-06-26 | Manifest 自由配置 Pipeline | 已采纳 |

## Bugfix 记录

| BUG | 日期 | 简述 | 等级 | 状态 |
|-----|------|------|------|------|
| [BUG-003](bugfixes/BUG-003-empty-models-crash.md) | 2026-06-26 | 空模型数组导致崩溃 | P0 | 已修复 |
```

> Proposal / BUG 列以 Markdown 链接直接指向对应文档路径，不再单独列出关联说明行，便于后续新增。

| 章节 | 记录内容 | 更新时机 |
|------|----------|----------|
| Feature 记录 | 本阶段关联的已采纳提议（编号、日期、简述、状态） | 提议采纳并补充到本阶段时追加一行 |
| Bugfix 记录 | 本阶段关联的已确认缺陷（编号、日期、简述、等级、状态） | 缺陷修复并关联到本阶段时追加一行 |

> **关联原则**：提议 / 缺陷关联到哪个阶段，取决于其改动涉及的模块归属。例如 Pipeline 相关改动关联 `phase2.md`，API 相关改动关联 `phase3.md`。跨模块的改动可在多个阶段记录中同时出现。

**AI 提示话术：**
```
针对阶段 N 帮我提供具体的实现方案，
比如使用的库、三方依赖、具体的数据结构方案等。
然后将方案写入 docs 下的阶段 N 文档中。
```

---

### Step 2 — 开发者评估文档

**目的：** 在开发之前，开发者阅读阶段文档，确认方案合理性，提出调整意见。

**常见评估问题举例：**
- 是否存在多模型组合任务需要在本阶段考虑？（答：分阶段，本阶段只做单链路）
- 某个依赖库的版本是否合适？
- 某个接口设计是否需要调整？

此步骤为纯沟通环节，无 AI 直接参与产出文件。

---

### Step 3 — 实现代码 + 单元测试

**目的：** 按照阶段文档逐一实现交付物。

**实现原则（来自 code_spec.md）：**
- 遵循 Google C++ Style Guide；
- 命名空间至少两层（`atlas::<module>::`）；
- 目录名全小写；
- 所有注释使用英文；
- 有意义的字面值使用 `constexpr` 常量，禁止魔术字符串；
- 错误处理使用 `ErrorCode` 返回值，禁止异常。

**单元测试要求：**
- 覆盖正常路径与关键异常路径；
- 测试数据放在 `tests/<module>/test_data/`；
- 测试文件命名为 `<module>_test.cc`。

**AI 提示话术：**
```
按照 docs 目录下的整体架构、代码规范以及阶段 N 的实现要求，
开始进行实现，包括相关的单元测试等。
```

**构建验证命令：**
```bash
bazel test //...
```

---

### Step 4 — 构建验证

**目的：** 确保所有测试通过，无编译错误，无测试失败。

**验证标准：**
- `bazel test //...` 全部 PASSED；
- 无新增编译 warning（`-Wall` 下）；
- 新增测试用例数符合阶段文档中的覆盖点要求。

---

### Step 5 — 编写阶段结果文档

**目的：** 记录本阶段的实际交付情况，作为项目历史存档。

**文档路径：** `docs/phase{N}_result.md`

**文档应包含：**
- 实际完成的交付物（与计划对比）
- 新增文件清单
- 测试结果汇总（测试套件数 / 用例数 / 全通过标记）
- 过程中遇到的问题及解决方案（供后续阶段参考）
- 与设计文档的差异说明（若有）

**AI 提示话术：**
```
阶段 N 已经完成，将本阶段的交付汇总更新写入 docs/phase{N}_result.md。
```

---

### Step 6 — git commit

**提交规范：**
- 格式：`feat: phase N — <简短描述>`
- commit body 使用 `-` 逐条列出本阶段新增/修改的主要内容；
- commit body 末尾附加 `Co-Authored-By` 或 `AI-Tool` trailer，标注当前开发所使用的 AI 工具类型及模型；
- 提交信息仅允许使用 ASCII 字符（禁止中文、emoji、非 ASCII 符号）
- 每个阶段一笔完整提交，保持 git log 清晰。

**AI 工具标注格式：**

在 commit body 末尾添加 trailer，格式为：

```
AI-Tool: <工具名称> / <模型名称>
```

示例：

```
feat: phase 6 — public shared library and Bazel integration

- Add src/public/ bridge layer with atlas_export.h symbol visibility control
- Add include/atlas/ public header set (atlas_runtime.h, model_handle.h, types.h, version.h)
- Add atlas_init.cc backend registration anchor for shared library builds
- Add tools/install_atlas.sh and atlas.pc.in for non-Bazel consumers
- Add examples/two_model_pipeline public API integration demo

AI-Tool: CodeBuddy / GLM-5.2
```

> 说明：项目早期阶段（phase 1-5）若已提交且未标注，无需追溯补充。自本规范生效后的提交须携带 `AI-Tool` trailer。

**AI 提示话术：**
```
目前阶段 N 已经完成，通过 git 执行一笔提交，
注意 .gitignore 是否需要更新，并在 commit body 末尾附加 AI-Tool trailer。
```

---

## 三、文档体系一览

```
docs/
├── architecture.md          # 总则：整体架构、模块说明、阶段划分
├── code_spec.md             # 代码规范（全局适用）
├── doc_spec.md              # 文档管理总则：编写规范与定期梳理
├── feature_spec.md          # Feature 管理总则：提议收集与流转规范
├── bugfix_spec.md           # Bugfix 管理总则：缺陷报告与修复规范
├── phase_spec.md            # 本文档：阶段开发规范与流程
├── proposals/               # 提议收集池（草案 → 评审 → 采纳/驳回）
│   ├── README.md
│   └── NNN-*.md
├── bugfixes/                # 缺陷报告池（BUG-NNN-*.md）
├── phase1.md                # 阶段一设计方案
├── phase1_result.md         # 阶段一结果汇总
├── phase2.md                # 阶段二设计方案
├── phase2_result.md         # 阶段二结果汇总
├── phase3.md                # 阶段三设计方案
├── phase3_result.md         # 阶段三结果汇总
├── phase5.md                # 阶段五设计方案（示例 + 基准测试 + 发布）
├── phase6.md                # 阶段六设计方案（对外库及接口）
└── phase{N}.md / result.md  # 后续阶段依此类推
```

---

## 四、已有阶段经验小结

### 阶段一经验

| 问题 | 解决方案 |
|------|----------|
| GoogleTest 1.14.0 依赖 Abseil | 降级至 1.12.1（最后一个无强制 Abseil 依赖版本） |
| `third_party/` 无 BUILD 文件导致 `build_file` 引用失败 | 在 `third_party/` 下补充空 BUILD 文件 |
| sha256 占位符导致构建失败 | 先用占位符让 Bazel 报告真实 hash，再回填 |

### 阶段二经验

| 问题 | 解决方案 |
|------|----------|
| `select()` 不能直接作为 `deps` 列表元素 | 改为 `deps = [...] + select({...})` |
| macOS dylib 运行时找不到版本化库 | `srcs` 中同时列出符号链接和版本化实体文件 |
| `ATLAS_REGISTER_BACKEND` 宏的 `##cls` 无法处理含 `::` 的全限定名 | 改用 `__COUNTER__` 生成唯一变量名，`cls` 仅用于 `make_unique<cls>()` |
| Python 路径计算错误导致测试模型生成到错误目录 | 确认从脚本文件位置起需要几层 `os.path.dirname` 到达仓库根目录 |
