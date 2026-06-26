# Bugfix 管理总则

本文档定义 Atlas 项目中缺陷（bug）的报告、分级、修复与验证流程，确保所有缺陷修复有据可查、由开发者引导 AI 完成。

---

## 一、总则

### 1.1 适用范围

本规范适用于 Atlas 项目中所有缺陷修复，包括但不限于：

- 功能性缺陷（推理结果错误、接口行为异常等）
- 编译 / 链接错误（构建失败、平台兼容性问题）
- 内存安全缺陷（泄漏、越界、空指针解引用等）
- 性能缺陷（非预期的延迟退化、内存占用异常）
- 文档缺陷（文档与代码不一致、示例无法运行）

> 纯文档笔误、格式修正可跳过本流程，直接提交。

### 1.2 核心原则

| 原则 | 说明 |
|------|------|
| **先报告，后修复** | 任何缺陷须先创建 bug report，经开发者确认后再由 AI 修复 |
| **开发者定级** | 缺陷的严重等级与修复优先级由开发者判定，AI 不自行决定 |
| **AI 执行不越界** | AI 不应主动修改代码修复缺陷；须由开发者引导发起 |
| **修复可验证** | 每个缺陷须有可复现的触发条件与验证方法（测试用例或手动步骤） |
| **闭环归档** | 修复完成后须更新 bug report 状态并记录在 CHANGELOG |

---

## 二、Bug 分级

根据缺陷对系统功能与用户的影响程度，分为四个等级：

| 等级 | 标识 | 定义 | 示例 | 修复时效 |
|------|------|------|------|----------|
| **致命** | P0 | 核心功能完全不可用，或存在数据损坏 / 安全风险 | 推理崩溃、模型加载失败、内存破坏 | 立即修复，阻塞发布 |
| **严重** | P1 | 核心功能部分不可用，或存在明确的错误结果 | 输出形状错误、dtype 转换错误、特定模型无法加载 | 当前迭代内修复 |
| **一般** | P2 | 非核心功能异常，或有 workaround 可规避 | 日志缺失、错误码不准确、特定平台警告 | 下一迭代修复 |
| **轻微** | P3 | 体验问题，不影响功能正确性 | 文档措辞、注释错误、代码风格 | 择机修复 |

> 分级由开发者确认。AI 在 bug report 中可建议初始等级，但最终判定权归开发者。

---

## 三、Bugfix 目录规范

### 3.1 目录结构

```
docs/
├── bugfixes/                        # 缺陷报告与修复记录池
│   ├── BUG-001-segmentation-fault-on-load.md
│   ├── BUG-002-wrong-output-shape.md
│   └── ...
├── proposals/                       # Feature 提议池
├── feature_spec.md
├── bugfix_spec.md                   # 本文档
└── ...
```

### 3.2 命名规则

```
BUG-NNN-short-kebab-title.md
```

- `BUG-NNN`：递增编号，从 `001` 开始，不回收、不重排
- `short-kebab-title`：英文小写短标题，连字符分隔，如 `segmentation-fault-on-load`
- 与 `proposals/` 的 `NNN-*.md` 编号体系独立，互不干扰

### 3.3 Bug Report 模板

每份 bug report 须包含以下结构：

```markdown
# BUG-NNN: <标题>

> **报告日期**：YYYY-MM-DD
> **报告人**：<姓名 / GitHub ID>
> **状态**：待确认 / 已确认 / 修复中 / 已修复 / 已驳回
> **等级**：P0 / P1 / P2 / P3
> **影响版本**：<受影响的版本或 git commit hash>
> **关联模块**：<如 src/backend/cpu、src/pipeline 等，多个用逗号分隔>

## 一、缺陷描述

<简要描述缺陷现象：发生了什么，预期应该是什么。>

## 二、复现步骤

1. <步骤一>
2. <步骤二>
3. <步骤三>

## 三、预期行为

<正确情况下应该发生什么。>

## 四、实际行为

<实际观察到的错误现象，附错误日志 / 堆栈（如有）。>

## 五、根因分析

<修复后填写：导致缺陷的根本原因。>

## 六、修复方案

<修复后填写：采用的修复方式及涉及文件。>

## 七、验证

<修复后填写：如何验证修复有效，关联的测试用例。>

## 八、后续动作

- [ ] 开发者确认等级与修复优先级
- [ ] AI 按方案修复 + 补充测试
- [ ] 更新 CHANGELOG.md
- [ ] 关闭并归档
```

---

## 四、状态流转

### 4.1 状态定义

| 状态 | 含义 | 触发条件 |
|------|------|----------|
| `待确认` | 缺陷已报告，等待开发者确认是否为真实 bug | 报告人创建文档 |
| `已确认` | 开发者确认缺陷存在，并判定等级与优先级 | 开发者审查后确认 |
| `修复中` | AI 正在按修复方案实施修复 | 开发者引导 AI 开始修复 |
| `已修复` | 修复已完成并通过验证 | 代码合并 + 测试通过 |
| `已驳回` | 经确认非缺陷（预期行为 / 无法复现 / 重复报告） | 开发者判定后关闭 |

### 4.2 流转规则

```
待确认 ──(开发者确认)──► 已确认 ──(引导AI修复)──► 修复中 ──(验证通过)──► 已修复
    │                                                      │
    └──(非缺陷/无法复现)──► 已驳回                          └──(验证失败)──► 回到修复中
```

- `已驳回` 的 report 须在文档末尾补充「驳回理由」；
- `已修复` 的 report 须完整填写第五至第七章（根因、方案、验证）；
- 状态变更时同步更新报告日期下方的状态字段。

---

## 五、修复流程

### 5.1 标准流程（P1 / P2 / P3）

```
发现缺陷
    │
    ▼
[Step 1] 创建 bugfixes/BUG-NNN-*.md，填写缺陷描述 + 复现步骤
    │
    ▼
[Step 2] 开发者确认缺陷，判定等级与优先级（状态：已确认）
    │
    ▼
[Step 3] 开发者引导 AI 分析根因，填写修复方案（状态：修复中）
    │
    ▼
[Step 4] AI 按方案修复代码 + 补充回归测试
    │
    ▼
[Step 5] bazel test //... 验证通过
    │
    ▼
[Step 6] 更新 bug report（根因 / 方案 / 验证）+ CHANGELOG.md
    │
    ▼
[Step 7] git commit（状态：已修复）
```

### 5.2 紧急流程（P0）

P0 缺陷允许简化流程以加快修复：

```
发现 P0 缺陷
    │
    ▼
[Step 1] 创建 bugfixes/BUG-NNN-*.md（至少填写描述 + 复现步骤）
    │
    ▼
[Step 2] 开发者确认 P0，立即引导 AI 修复（状态：修复中）
    │         ↓ 可与 Step 3 并行
    ▼
[Step 3] AI 修复代码 + 补充回归测试
    │
    ▼
[Step 4] bazel test //... 验证通过
    │
    ▼
[Step 5] 补全 bug report（根因 / 方案 / 验证）+ CHANGELOG.md
    │
    ▼
[Step 6] git commit（状态：已修复）
```

> P0 与标准流程的区别：允许先修复后补全文档，但最终状态必须为「已修复」且文档完整。

### 5.3 何时可跳过 Bug Report

以下情况可直接修复，无需创建 bug report，但须在 commit message 中说明：

| 情况 | commit 格式 |
|------|-------------|
| 纯文档笔误 | `fix: correct typo in phase2.md` |
| CI 配置修复 | `fix: ci — update bazel version` |
| 构建脚本微调 | `fix: adjust include path in BUILD` |

---

## 六、回归测试要求

| 缺陷等级 | 回归测试要求 |
|----------|-------------|
| P0 | **必须**新增或补充单元测试，覆盖触发该缺陷的输入条件 |
| P1 | **必须**新增或补充单元测试 |
| P2 | 建议补充测试，至少在 commit message 中说明验证方法 |
| P3 | 可选 |

测试文件命名与放置遵循 `code_spec.md`：
- 放在 `tests/<module>/` 下
- 命名为 `<module>_test.cc`（已有则追加用例）

> 若缺陷属于难以编写自动化测试的场景（如特定硬件平台问题），须在 bug report 的「验证」章节说明手动验证步骤。

---

## 七、提交规范

### 7.1 Commit 格式

```
fix: BUG-NNN — <简短描述>
```

- `fix` 前缀标识 bugfix 提交（区别于 feature 的 `feat`）；
- `BUG-NNN` 关联 bug report 编号；
- commit body 使用 `-` 逐条列出修改内容；
- commit body 末尾附加 `AI-Tool` trailer（同 `phase_spec.md` 规范）。

### 7.2 示例

```
fix: BUG-003 — fix crash when manifest has empty models array

- Add empty-array check in ManifestParser::Parse()
- Add regression test: EmptyModelsArrayReturnsError
- Update manifest_parser_test.cc

AI-Tool: CodeBuddy / GLM-5.2
```

### 7.3 CHANGELOG 记录

每个 bugfix 须在 `CHANGELOG.md` 的 `### Fixed` 小节下记录：

```markdown
### Fixed
- BUG-003: Crash when manifest contains empty models array (P0)
- BUG-004: Incorrect output shape for dynamic-dimension models (P1)
```

---

## 八、与现有文档体系的关系

```
bugfixes/（缺陷报告）          proposals/（Feature 提议）
       │                              │
       ▼                              ▼
  开发者确认 + 引导              开发者评审 + 采纳
       │                              │
       ▼                              ▼
  AI 修复代码 + 测试            AI 实现代码 + 测试
       │                              │
       └──────────┬───────────────────┘
                  ▼
           CHANGELOG.md（统一记录）
```

| 文档 | 职责 |
|------|------|
| `bugfixes/BUG-NNN-*.md` | 缺陷报告与修复全程记录 |
| `bugfix_spec.md` | 本文档：bugfix 管理规范 |
| `feature_spec.md` | Feature 提议管理规范 |
| `phase_spec.md` | 阶段开发流程规范（含 commit 规范） |
| `CHANGELOG.md` | 面向用户的版本变更记录（Fixed 小节） |

> 若一个 bug 的根因涉及架构设计缺陷，修复方案可能演变为 Feature 提议（如「Pipeline 不支持多输入」的 bug 导致 Proposal 立项）。此时 bug report 中标注 `→ Proposal-NNN`，状态保持「已确认」并附注「修复方案转为 Feature 流程」。

---

## 九、实践要点

### 9.1 Bug 与 Feature 的边界

| 判据 | 分类 |
|------|------|
| 代码行为与文档描述不一致 | Bug |
| 代码行为符合文档，但文档设计本身有缺陷 | Feature（需提议改进设计） |
| 新发现的边界场景未处理，但属合理遗漏 | Bug（补充处理） |
| 需要新增能力才能解决 | Feature |

### 9.2 重复 Bug 处理

- 发现重复报告时，保留编号较小的那份为主 report；
- 在重复 report 中标注 `重复于 BUG-NNN`，状态改为「已驳回」，驳回理由填「与 BUG-NNN 重复」；
- 不删除重复 report 文件。

### 9.3 无法复现的 Bug

- 状态保持「待确认」；
- 在 report 中记录已尝试的复现环境与步骤；
- 超过 2 周无法复现，开发者可决定关闭（状态改为「已驳回」，理由「无法复现」）或降级处理。

---

## 十、快速上手 Checklist

缺陷从发现到修复的完整步骤：

- [ ] 1. 在 `docs/bugfixes/` 创建 `BUG-NNN-short-title.md`，填写描述 + 复现步骤
- [ ] 2. 开发者确认缺陷，判定等级（P0-P3）与优先级
- [ ] 3. 开发者引导 AI 分析根因，确定修复方案
- [ ] 4. AI 修复代码 + 补充回归测试（P0/P1 必须）
- [ ] 5. `bazel test //...` 验证通过
- [ ] 6. 补全 bug report（根因 / 方案 / 验证章节）
- [ ] 7. 更新 `CHANGELOG.md` 的 `### Fixed` 小节
- [ ] 8. git commit（`fix: BUG-NNN — ...` + `AI-Tool` trailer）
- [ ] 9. bug report 状态改为「已修复」，归档
