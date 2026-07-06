# Proposal-010: Locale-safe 字符串解析工具函数

> **提议日期**：2026-07-06
> **提议人**：Cline
> **状态**：已采纳
> **类型**：小型
> **关联**：`docs/code_spec.md` §十一（平台兼容性）、BUG-002、BUG-003

## 一、背景

在 Android NDK 环境中，`std::locale` 的静态初始化顺序在不同翻译单元之间是未定义的，访问尚未初始化的 locale facet 会导致 `std::bad_cast` 崩溃。

`docs/code_spec.md` §11.1 已明确禁止在可能运行于 Android 的代码中使用 `std::stringstream` / `std::ifstream`，并建议优先使用 C 标准库函数处理字符串解析。BUG-002 和 BUG-003 已分别修复了 `normalize_node.cc` 和 `manifest_parser.cc` 中的相关违规。

然而，当前工程中仍有 4 处 `std::stoi` 散落在各模块中：

| 文件 | 行号 | 使用 |
|------|------|------|
| `src/pipeline/nodes/topk_node.cc` | 87 | `CreateFromParams` 中解析 `k` |
| `src/pipeline/nodes/resize_node.cc` | 150-151 | `CreateFromParams` 中解析 `height`/`width` |
| `src/pipeline/nodes/softmax_node.cc` | 114 | `CreateFromParams` 中解析 `axis` |
| `src/backend/cpu/cpu_backend.cc` | 64 | `CpuBackend::Load` 中解析 `num_threads` |

虽然 `std::stoi` 在运行时路径中一般安全，但：
1. 与规范"优先使用 C 标准库"的指导原则不一致；
2. 依赖异常机制进行错误处理——这与项目"禁止异常、返回 `ErrorCode`"的总体风格冲突；
3. 散落的 `try/catch` 块增加认知负担，缺乏统一的错误处理模式。

## 二、方案概要

在 `src/utils/` 下新增 `Strings` 工具类（文件 `strings.h` / `strings.cc`），提供基于 C 标准库的字符串解析静态方法，遵循 Google C++ Style 命名规范。所有方法：
- 返回 `ErrorCode`，无异常抛出
- 底层使用 `std::strtol`，零 locale 依赖
- 全面校验：空输入、无效字符、溢出均返回 `kInvalidArgument`

### 2.1 类定义

```cpp
namespace atlas {
namespace utils {

class Strings {
 public:
    Strings() = delete;

    // 解析十进制整数，成功返回 kOk，失败返回 kInvalidArgument。
    static ErrorCode ParseInt(const std::string& s, int* out);
    static ErrorCode ParseInt(const char* s, int* out);

    // 带基数版本，默认 10。
    static ErrorCode ParseInt(const std::string& s, int* out, int base);
    static ErrorCode ParseInt(const char* s, int* out, int base);
};

}  // namespace utils
}  // namespace atlas
```

### 2.2 实现要点

```cpp
ErrorCode Strings::ParseInt(const char* s, int* out, int base) {
    if (s == nullptr || out == nullptr) return ErrorCode::kInvalidArgument;
    char* end = nullptr;
    errno = 0;
    long val = std::strtol(s, &end, base);  // NOLINT(runtime/int)
    if (errno == ERANGE) return ErrorCode::kInvalidArgument;  // overflow
    if (end == s) return ErrorCode::kInvalidArgument;         // no digit
    if (*end != '\0') return ErrorCode::kInvalidArgument;     // trailing chars
    *out = static_cast<int>(val);
    return ErrorCode::kOk;
}
```

### 2.3 文件结构

| 文件 | 内容 |
|------|------|
| `src/utils/strings.h` | 类声明 + Doxygen 注释 |
| `src/utils/strings.cc` | 实现 |
| `tests/utils/strings_test.cc` | 单元测试 |

### 2.4 替换计划

| 文件 | 原代码 | 替换为 |
|------|--------|--------|
| `topk_node.cc` | `std::stoi(it->second)` + `try/catch` | `Strings::ParseInt(it->second, &k)` + `if` 检查 |
| `resize_node.cc` | `std::stoi(h_it->second)` + `try/catch` | `Strings::ParseInt(h_it->second, &height)` + `if` 检查 |
| `softmax_node.cc` | `std::stoi(it->second)` + `try/catch` | `Strings::ParseInt(it->second, &axis)` + `if` 检查 |
| `cpu_backend.cc` | `std::stoi(it->second)` + `try/catch` | `Strings::ParseInt(it->second, &num_threads)` + `if` 检查 |

## 三、影响范围

| 维度 | 影响 |
|------|------|
| 新增模块 | `src/utils/strings.h` / `.cc`，`tests/utils/strings_test.cc` |
| 修改模块 | `topk_node.cc`、`resize_node.cc`、`softmax_node.cc`、`cpu_backend.cc` |
| BUILD 变更 | `src/utils/BUILD`、`src/backend/cpu/BUILD`、`src/pipeline/nodes/BUILD`、`tests/utils/BUILD` |
| 文档变更 | `docs/code_spec.md` §11.1 "替代方案"列补充 `atlas::utils::Strings` |
| 公共 API | 无影响 |
| 新依赖 | 无（仅 C 标准库） |
| 预估工作量 | 小（已实现） |

## 四、后续动作

- [x] 评审讨论（2026-07-06，确认通过）
- [x] 实现：`src/utils/strings.h` / `strings.cc` + 单元测试
- [x] 替换 4 处 `std::stoi` 为 `Strings::ParseInt`
- [x] 更新 `docs/code_spec.md` §11.1 替代方案列