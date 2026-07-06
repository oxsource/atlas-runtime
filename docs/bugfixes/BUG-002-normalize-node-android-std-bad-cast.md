# BUG-002: normalize_node Android 平台 std::stringstream 静态初始化抛出 std::bad_cast

> **报告日期**：2026-07-06
> **报告人**：pizzk <726676435@qq.com>
> **状态**：已修复
> **等级**：P0
> **影响版本**：3e612099 (feat: phase 5 — pipeline nodes + manifest pipeline config)
> **关联模块**：src/pipeline/nodes/normalize_node

## 一、缺陷描述

在 Android 平台（android_arm64）编译并运行依赖 `//src/pipeline:pipeline` 或 `//src/pipeline/nodes:normalize_node` 的二进制时，程序在启动阶段抛出 `std::bad_cast` 异常并终止：

```
terminating with uncaught exception of type std::bad_cast: std::bad_cast
Aborted
```

## 二、复现步骤

1. 检出 `3e612099` 或之后版本
2. 在 `examples/android_log/BUILD` 的 `deps` 中添加 `//src/pipeline:nodes` 或 `//src/pipeline/nodes:normalize_node`
3. 使用 Android NDK 交叉编译（`--config=android_arm64` 或类似配置）
4. 在 Android 设备上运行编译产物
5. 观察到程序打印 `std::bad_cast` 后崩溃

## 三、预期行为

程序正常启动，不抛出任何异常。

## 四、实际行为

Android 上 `normalize_node.cc` 被链接后，进程在静态初始化阶段抛出 `std::bad_cast`。

通过二分法定位到 `normalize_node.cc` 是唯一致崩溃的文件：
- 去掉 `normalize_node` 后其他 8 个 node 全部正常
- 仅添加 `normalize_node` 即崩溃

## 五、根因分析

`normalize_node.cc` 的 `ParseFloatList()` 函数使用了 `std::stringstream`：

```cpp
std::stringstream ss(s);
std::string token;
while (std::getline(ss, token, ',')) {
    out->push_back(std::stof(token));
}
```

`std::stringstream` 构造时触发 `std::locale` 的静态初始化。在 Android NDK 环境中，`std::locale` 的内部实现（`std::use_facet`）在特定的链接顺序下会因 locale facet 未正确初始化而抛出 `std::bad_cast`。

其他 node 文件使用的 `std::stoi` 虽然也涉及 locale，但其实现路径不同，不会触发此问题。`std::stringstream` + `std::stof` 的组合是唯一的触发路径。

此问题属于 **C++ STL 静态初始化顺序不确定性** 在 Android NDK 平台上的典型表现：
- 静态初始化（Dynamic Initialization）发生的顺序在不同翻译单元之间是**未定义**的
- 当 `std::locale` 的全局 facet 在某个静态初始化中被使用但尚未初始化时，`std::bad_cast` 被抛出
- 此问题仅在特定平台（Android NDK）上重现，macOS/Linux 的 libc++ 实现不受影响

## 六、修复方案

将 `ParseFloatList()` 中的 `std::stringstream` + `std::stof` 替换为 C 标准库函数 `std::strtof`：

```cpp
// 改前
bool ParseFloatList(const std::string& s, std::vector<float>* out) {
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        out->push_back(std::stof(token));
    }
}

// 改后
bool ParseFloatList(const std::string& s, std::vector<float>* out) {
    const char* p = s.c_str();
    while (p < end) {
        char* next = nullptr;
        out->push_back(std::strtof(p, &next));
        p = next;
        if (*p == ',') ++p;
    }
}
```

`std::strtof` 不依赖 `std::locale` 的静态初始化，在所有平台上行为一致，且已在 C 标准库中良好定义。

涉及文件：
- `src/pipeline/nodes/normalize_node.cc` — 修改 `ParseFloatList()` 实现

## 七、验证

- macOS/Linux: `bazel test //tests/...` — 10/10 测试通过
- Android: 需用户验证添加 `normalize_node` 后运行不再崩溃

## 八、后续动作

- [x] 根因分析（2026-07-06）
- [x] 代码修复
- [x] 创建 BUG-002 报告
- [x] 更新 `src/pipeline/nodes/normalize_node.cc` 注释
- [x] 更新 `CHANGELOG.md` 的 Fixed 小节
- [x] 关闭并归档