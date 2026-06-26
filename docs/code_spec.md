# Atlas 代码规范

## 一、总则

本项目遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 作为基础规范。
在此之上，结合项目结构与嵌入式/边缘设备的实际需求，补充以下约定。
**所有贡献者在提交代码前须通过 `cpplint` 检查。**

---

## 二、目录与文件命名

### 2.1 目录命名

> ⚠️ **目录名一律使用小写字母，禁止出现大写字母。**

- 多个单词之间使用下划线 `_` 分隔；
- 不使用连字符 `-`、空格或驼峰。

```
# 正确
src/backend/base/
src/core/
src/utils/
tests/core/

# 错误
src/Backend/
src/backendBase/
src/BackendBase/
```

### 2.2 文件命名

遵循 Google 规范，使用全小写 + 下划线：

| 类型 | 命名规则 | 示例 |
|------|----------|------|
| 头文件 | `snake_case.h` | `manifest_parser.h` |
| 实现文件 | `snake_case.cc` | `manifest_parser.cc` |
| 测试文件 | `<module>_test.cc` | `manifest_parser_test.cc` |
| Bazel BUILD 文件 | 固定大写 `BUILD` | `BUILD` |

---

## 三、命名空间

> ⚠️ **命名空间至少使用两层，禁止将代码直接放在顶层 `atlas` 命名空间之外。**

### 3.1 层级约定

```
atlas::          —— 顶层命名空间（整个项目）
atlas::core::    —— 核心模块（清单解析、模型管理等）
atlas::backend:: —— 后端抽象与各平台实现
atlas::pipeline:: —— 预处理/后处理管线
atlas::api::     —— 对外暴露的公共接口
atlas::utils::   —— 公共工具类、类型定义
```

### 3.2 使用规则

```cpp
// 正确：两层命名空间
namespace atlas {
namespace core {

class ManifestParser { ... };

} // namespace core
} // namespace atlas

// 错误：只有一层
namespace atlas {
class ManifestParser { ... };
}

// 错误：匿名命名空间内的公共符号
namespace {
class ManifestParser { ... };  // 仅允许用于文件内部实现细节
}
```

- 头文件中**禁止** `using namespace`；
- `.cc` 实现文件中可在函数体内使用 `using`，不可置于文件作用域；
- 命名空间结束花括号后必须附注释：`} // namespace xxx`。

---

## 四、类与接口命名

遵循 Google 规范，使用 `PascalCase`：

```cpp
class ManifestParser {};      // 具体类
class IBackend {};            // 抽象接口：I 前缀
class BackendFactory {};      // 工厂类
struct ModelConfig {};        // 纯数据结构用 struct
struct TensorInfo {};
```

- 抽象接口类统一以 `I` 为前缀（`IBackend`、`IPipeline`）；
- 纯数据聚合（无行为）使用 `struct`，其余使用 `class`。

---

## 五、函数与变量命名

| 元素 | 规则 | 示例 |
|------|------|------|
| 普通函数 / 方法 | `PascalCase` | `Load()`, `GetInputInfo()` |
| 局部变量 | `snake_case` | `model_path`, `input_size` |
| 成员变量 | `snake_case_` （尾下划线） | `model_path_`, `is_loaded_` |
| 静态成员变量 | `s_snake_case_` | `s_instance_` |
| 常量 / constexpr | `kPascalCase` | `kMaxModels`, `kDefaultLayout` |
| 枚举值 | `kPascalCase` | `kFloat32`, `kOk` |
| 宏 | `ATLAS_ALL_CAPS` | `ATLAS_REGISTER_BACKEND` |

---

## 六、头文件规范

- 每个头文件有且仅有一个 `#pragma once`（禁止使用 `#ifndef` 守卫）；
- 包含顺序（每组之间空一行）：
  1. 对应的 `.h` 文件（仅在 `.cc` 中）
  2. C 标准库头文件（`<cstdint>` 等）
  3. C++ 标准库头文件（`<string>`, `<vector>` 等）
  4. 三方库头文件（`<nlohmann/json.hpp>` 等）
  5. 项目内头文件（使用项目根相对路径）

```cpp
// manifest_parser.cc 示例
#include "src/core/manifest_parser.h"

#include <cstdlib>

#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "src/utils/types.h"
```

---

## 七、错误处理

- **禁止**使用 C++ 异常（`throw` / `catch`）；
- 所有公共接口返回 `atlas::utils::ErrorCode`；
- 函数内部通过返回值或输出参数传递错误状态；
- 必要时使用日志宏记录错误上下文，但不在库代码中直接调用 `std::cerr`。

```cpp
// 正确
atlas::utils::ErrorCode ManifestParser::Parse(
    const std::string& path, ManifestConfig* config);

// 错误
ManifestConfig ManifestParser::Parse(const std::string& path);  // 异常抛出
```

---

## 八、常量与魔术值

> ⚠️ **所有有意义的字面值（数字、字符串）必须以具名常量表达式替代，禁止在代码中直接使用魔术值。**

- 使用 `constexpr` 定义编译期常量；字符串常量使用 `constexpr std::string_view` 或 `constexpr const char*`；
- 枚举值优先于整型字面量表示有限集合；
- 允许的例外：`0`/`1` 用于初始化、循环边界等语义明确的数学上下文，以及 `nullptr`。

```cpp
// Correct
constexpr int kSupportedMajorVersion = 1;
constexpr std::string_view kDefaultLayout = "NCHW";
constexpr std::string_view kDtypeFloat32  = "float32";

if (major != kSupportedMajorVersion) { ... }
info->layout = std::string(kDefaultLayout);

// Wrong — magic values scattered in logic
if (major != 1) { ... }
if (dtype == "float32") { ... }   // literal string without named constant
```

---

## 九、内存管理

- 优先使用 `std::unique_ptr` 管理堆对象所有权；
- `std::shared_ptr` 仅在确实需要共享所有权时使用；
- **禁止**裸 `new` / `delete` 出现在业务逻辑中（仅允许在 `Tensor` 等底层数据结构内部）；
- `Tensor` 结构禁止拷贝构造/赋值，仅允许移动语义，避免大块数据冗余拷贝。

---

## 十、Bazel 构建规范

- 每个目录下的 `BUILD` 文件只描述本目录的目标；
- 目标可见性默认 `//visibility:private`，需跨目录使用时显式声明；
- 三方依赖统一在 `third_party/` 目录下管理，不直接在业务 `BUILD` 文件中写 `http_archive`；
- 测试目标命名为 `<module>_test`，放在 `tests/` 对应子目录下。

```python
# 示例：src/core/BUILD
cc_library(
    name = "manifest_parser",
    srcs = ["manifest_parser.cc"],
    hdrs = ["manifest_parser.h", "manifest_config.h"],
    deps = [
        "//src/utils:types",
        "@nlohmann_json//:json",
    ],
    visibility = ["//visibility:public"],
)
```

---

## 十、注释规范

- **代码中所有注释（包括 Doxygen、行内注释、块注释）一律使用英文**；
- 公共头文件中的类、接口、公共方法使用 Doxygen 风格注释；
- 实现文件中复杂逻辑段落使用行内注释说明**意图**而非重述代码；
- **禁止**提交注释掉的废弃代码，应通过 git 历史追溯。

```cpp
/// @brief Parse the manifest file and populate ManifestConfig.
/// @param path   Absolute path to the manifest file.
/// @param config Output parameter for the parsed result, must not be nullptr.
/// @return kOk on success; see ErrorCode for other values.
ErrorCode Parse(const std::string& path, ManifestConfig* config);

// Expand environment variables in model_path before validation.
ExpandEnvVars(&model_config.model_path);
```

---

## 十一、规范检查工具

| 工具 | 用途 | 执行方式 |
|------|------|----------|
| `cpplint` | Google 风格静态检查 | `cpplint --recursive src/` |
| `clang-format` | 代码格式化 | 项目根提供 `.clang-format`，提交前执行 |
| `clang-tidy` | 静态分析（可选） | CI 阶段运行 |

`.clang-format` 基础配置：

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
```
