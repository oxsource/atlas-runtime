# BUG-003: manifest_parser Android 平台 std::istringstream 静态初始化抛出 std::bad_cast

> **报告日期**：2026-07-06
> **报告人**：pizzk <726676435@qq.com>
> **状态**：已修复
> **等级**：P0
> **影响版本**：3e612099 (feat: phase 5 — pipeline nodes + manifest pipeline config)
> **关联模块**：src/core/manifest_parser

## 一、缺陷描述

在 Android 平台（android_arm64）编译并运行依赖 `//src/api:atlas_runtime` 的二进制时，程序在启动阶段抛出 `std::bad_cast` 异常并终止：

```
terminating with uncaught exception of type std::bad_cast: std::bad_cast
Aborted
```

与 BUG-002 表现一致，但根因在 `manifest_parser.cc` 而非 `normalize_node.cc`。

## 二、复现步骤

1. 检出 `3e612099` 或之后版本
2. 在 `examples/android_log/BUILD` 的 `deps` 中添加 `//src/api:atlas_runtime`
3. 使用 Android NDK 交叉编译（`--config=android_arm64` 或类似配置）
4. 在 Android 设备上运行编译产物
5. 观察到程序打印 `std::bad_cast` 后崩溃

注：子依赖 `//src/core:manifest_parser`、`//src/core:model_manager`、`//src/api:model_handle` 等单独链接时正常，仅 `//src/api:atlas_runtime` 作为整体时触发。

## 三、预期行为

程序正常启动，不抛出任何异常。

## 四、实际行为

Android 上 `manifest_parser.cc` 被链接后，进程在静态初始化阶段抛出 `std::bad_cast`。

## 五、根因分析

`manifest_parser.cc` 的 `ParseVersionString()` 函数使用了 `std::istringstream`：

```cpp
bool ParseVersionString(const std::string& version, int* major, int* minor) {
    std::istringstream ss(version);
    char dot = 0;
    return static_cast<bool>(ss >> *major >> dot >> *minor) && dot == '.';
}
```

`std::istringstream` 构造时触发 `std::locale` 的静态初始化。在 Android NDK 环境中，`std::locale` 的内部实现（`std::use_facet`）在特定的链接顺序下会因 locale facet 未正确初始化而抛出 `std::bad_cast`。

此问题与 BUG-002 属于同一类 **C++ STL locale 初始化问题** 在 Android NDK 平台上的表现：

- `normalize_node.cc` 使用 `std::stringstream` + `std::stof` → BUG-002（静态初始化阶段触发）
- `manifest_parser.cc` 有**两个**触发点：
  1. `ParseVersionString()` 中的 `std::istringstream` — 静态初始化阶段可能触发
  2. `ManifestParser::Parse()` 中的 `std::ifstream` — **运行时代码路径**触发，`std::ifstream` 构造时在运行时初始化 `std::locale`，在 Android NDK 上抛出 `std::bad_cast`

通过二分法验证：注释掉 `parser_->Parse()` 调用后 crash 消失，确认 `std::ifstream` 是真正的触发源。
`std::istringstream` 修复作为预防性安全措施保留。

## 六、修复方案

做了两处替换，彻底移除 `manifest_parser.cc` 中的 `<sstream>` 和 `<fstream>` 依赖：

### 1. `ParseVersionString()`: `std::istringstream` → `std::sscanf`

```cpp
// 改前
bool ParseVersionString(const std::string& version, int* major, int* minor) {
    std::istringstream ss(version);
    char dot = 0;
    return static_cast<bool>(ss >> *major >> dot >> *minor) && dot == '.';
}

// 改后
bool ParseVersionString(const std::string& version, int* major, int* minor) {
    return std::sscanf(version.c_str(), "%d.%d", major, minor) == 2;
}
```

### 2. `ManifestParser::Parse()` 文件读取: `std::ifstream` → `fopen`/`fread` + `json::parse(string)`

```cpp
// 改前
std::ifstream file(path);
file >> j;

// 改后
FILE* file = std::fopen(path.c_str(), "rb");
// ... fseek/ftell/rewind/fread ...
nlohmann::json j = nlohmann::json::parse(content);
```

`std::ifstream` 构造时在运行时初始化 `std::locale`，这是实际触发 `std::bad_cast` 的根因。使用 C 标准库的 `fopen`/`fread` 读取文件内容到字符串，再通过 `nlohmann::json::parse(string)` 解析，完全规避了 locale 初始化路径。

涉及文件：
- `src/core/manifest_parser.cc` — 替换 `ParseVersionString()` 和文件读取实现，移除 `<sstream>` 和 `<fstream>`，增加 `<cstdio>`

## 七、验证

- macOS/Linux: `bazel test //tests/...` — 10/10 测试通过
- Android: 需用户验证 `//src/api:atlas_runtime` 运行不再崩溃

## 八、后续动作

- [x] 根因分析（2026-07-06）
- [x] 代码修复
- [x] 创建 BUG-003 报告
- [x] 更新 `CHANGELOG.md` 的 Fixed 小节
- [ ] 关闭并归档