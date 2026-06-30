# Proposal-006: ModelHandle 暴露 ModelConfig 关键字段

> **提议日期**：2026-06-30
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：模块级
> **关联**：`docs/phase3.md` API 模块、`src/core/manifest_config.h`

## 一、背景

当前 `ModelConfig`（定义于 `src/core/manifest_config.h`）包含模型的后端名称、模型文件路径、加载策略、后端专用配置等关键信息。这些字段仅在内部 `ModelManager` 持有，外部使用者无法通过公共 API 查询到。

`ModelHandle` 是外部使用者获取模型信息的唯一入口，目前仅暴露 `GetInputInfo()` / `GetOutputInfo()`（返回 `TensorInfo`），无法获取以下信息：

| 字段 | 类型 | 用途 |
|------|------|------|
| `backend` | `std::string` | 确认模型使用的后端（如 "cpu"、"snpe"），调试/日志需要 |
| `model_path` | `std::string` | 模型文件实际路径，审计/日志需要 |
| `load_strategy` | `LoadStrategy` | 确认模型是 eager 还是 lazy 加载 |
| `config` | `unordered_map<string,string>` | 后端专用 key-value 选项（如 SNPE 的 runtime_order），排查配置问题时需要 |

外部使用者（如 SDK 集成方）在以下场景需要这些信息：

1. **日志/监控**：记录每个模型的实际后端和模型路径；
2. **调试**：确认 manifest 配置是否正确生效（尤其是 lazy load 策略和后端 config）；
3. **审计**：排查生产环境中模型加载的配置来源。

## 二、方案概要

在 `ModelHandle` 上新增 4 个只读查询方法，通过 `ModelEntry.config` 获取对应字段。不暴露 `ModelConfig` 结构体本身，保持 PIMPL 分层设计。

```cpp
// 新增方法（声明于 src/public/include/atlas/model_handle.h）

// 返回模型后端名称，如 "cpu"。IsValid() == false 时返回空字符串。
std::string GetBackend() const;

// 返回模型文件路径（环境变量已展开）。IsValid() == false 时返回空字符串。
std::string GetModelPath() const;

// 返回加载策略：0 = eager, 1 = lazy。IsValid() == false 时返回 0。
int GetLoadStrategy() const;

// 返回后端专用 key-value 配置表。IsValid() == false 时返回空 map。
std::unordered_map<std::string, std::string> GetConfig() const;
```

实现层（`src/api/model_handle.cc`）通过 `entry_->config` 直接读取对应字段，无需引入新类型或修改 `ModelManager`。

对 `LoadStrategy` 的处理：内部枚举 `LoadStrategy` 属于 `core` 命名空间，不暴露到公共头文件。公共 API 用 `int` 返回（0/1），语义通过文档说明。

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 公共 API | `ModelHandle` 新增 4 个 const 方法，向后兼容 |
| 内部模块 | `src/api/model_handle.h` / `.cc` 实现层适配 |
| 清单格式 | 无变更 |
| 新增依赖 | 无 |
| ABI | 新增方法，SO 符号表增加 4 个导出符号，向后兼容 |
| 预估工作量 | 极小（约 20 行实现代码 + 测试用例补充） |

## 四、后续动作

- [ ] 评审讨论（记录日期与结论）
- [ ] 阶段归属：
  - `→ phase3.md`（补充）
- [ ] 若采纳：在 `ModelHandle` 上实现 + 补充单元测试
- [ ] 若驳回：填写驳回理由