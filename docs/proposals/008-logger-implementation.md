# Proposal-008: 日志输出管理系统

> **提议日期**：2026-07-03
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：模块级
> **关联**：`docs/architecture.md` §3.2（`src/utils/` 规划）、`docs/code_spec.md` §七（日志宏使用规范）、`docs/feature_spec.md` §2.1（日志库接入）

---

## 一、背景

### 1.1 当前现状

Atlas 项目目前**没有任何自身的日志系统**：

- `src/utils/` 目录在 `architecture.md` 中已规划包含「日志、错误码、类型定义、Tensor」，但当前仅实现了 `types.h`、`version.h/cc`，日志模块尚未落地；
- `code_spec.md` §七明确要求「必要时使用日志宏记录错误上下文，但不在库代码中直接调用 `std::cerr`」，但项目内未定义任何日志宏；
- `feature_spec.md` §2.1 已将「日志库接入」列入模块级改进方向；
- 源码中仅在两个外部 SDK 的初始化中接间接触及日志概念：
  - CPU 后端：`ORT_LOGGING_LEVEL_WARNING`（属 ONNX Runtime SDK 内部日志级别，非 Atlas 自身日志）
  - SNPE v2 后端：`SNPEFactory::initializeLogging()` / `terminateLogging()`（属 SNPE SDK 内部日志，非 Atlas 自身日志）

### 1.2 痛点

| 痛点 | 说明 |
|------|------|
| **调试困难** | 后端初始化、模型加载、推理执行全过程无结构化日志输出，异常排查依赖 `printf` 或 `gdb` |
| **平台不统一** | 桌面端可用 stdout，Android 端需走 logcat，iOS 需走 os_log，当前无统一抽象 |
| **无级别控制** | 调试信息与生产日志混杂，无法按场景开关（开发期全开，Release 仅 Error/Warn） |
| **规范未落地** | `code_spec.md` 要求使用日志宏，但无法让开发者遵循 |
| **外部 SDK 日志不整合** | ONNX Runtime / SNPE / TensorRT 等后端 SDK 各有自己的日志配置，无法统一管理 |

### 1.3 参考实现

参考 `filament_avm` 项目的 `falcon/core/utils/logger.h/cc` 实现，其特点为：

- **零外部依赖**：仅依赖标准 C/C++ 库（Android 平台额外链接 `-llog`）
- **平台适配**：Android → `__android_log_print()`（logcat），其他平台 → `std::printf` + 时间戳
- **宏 + 类双接口**：便捷宏 `FALCON_LOGI/D/W/E` + 实例化 `Log` 类
- **编译级过滤**：`base_level_` 运行时过滤，低于阈值的日志不输出
- **TAG 机制**：通过 `LOG_TAG` 宏自定义 TAG，默认使用 `__FILE__`

---

## 二、方案概要

### 2.1 核心思路

在 `src/utils/` 下新增 `logger.h` 和 `logger.cc`，提供一个**轻量、零外部依赖、跨平台**的日志输出管理系统。单元测试遵循项目规范，放在 `tests/utils/` 目录下：

```
src/utils/                          # 实现
├── BUILD
├── logger.h          # 日志头文件（宏定义 + Logger 类声明）
├── logger.cc         # 日志实现（平台特定输出）
├── types.h
├── version.h
└── version.cc

tests/utils/                        # 单元测试
├── BUILD
└── logger_test.cc    # 日志单元测试
```

### 2.2 设计原则

1. **零外部依赖**：不引入 spdlog / glog 等第三方日志库，保持 Atlas 依赖最小化
2. **平台自适应**：Android 输出到 logcat，macOS/Linux 输出到 stdout（带时间戳），iOS 走 os_log
3. **低侵入性**：通过宏封装，调用方式简洁，不影响现有代码结构
4. **编译期可裁剪**：可通过编译宏完全移除日志代码（Release 构建优化）
5. **统一管理**：整合各后端 SDK 的日志级别配置，提供全局日志策略

---

## 三、详细设计

### 3.1 日志级别

```cpp
enum class LogLevel {
    Debug,   // 调试信息，仅开发期开启
    Info,    // 常规信息（初始化、状态变更）
    Warn,    // 警告（非致命异常）
    Error,   // 错误（操作失败）
};
```

对应便捷宏：

```cpp
#define ATLAS_LOGD(fmt, ...)  // Debug
#define ATLAS_LOGI(fmt, ...)  // Info
#define ATLAS_LOGW(fmt, ...)  // Warn
#define ATLAS_LOGE(fmt, ...)  // Error
```

### 3.2 TAG 机制

```cpp
// 使用方式：在 #include "utils/logger.h" 之前定义 LOG_TAG
#define LOG_TAG "Atlas::CpuBackend"
#include "utils/logger.h"

// 未定义 LOG_TAG 时，默认使用 __FILE__（源文件名）
#ifndef LOG_TAG
#  define ATLAS_LOG_TAG __FILE__
#else
#  define ATLAS_LOG_TAG LOG_TAG
#endif
```

### 3.3 API 设计

```cpp
namespace atlas {
namespace utils {

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    // 实例接口（绑定固定 TAG）
    explicit Logger(const char* tag);
    void Debug(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);

    // 静态接口（宏使用此接口，每次传入 TAG）
    static void Log(Level level, const char* tag, const char* fmt, ...);

    // 全局日志级别控制
    static void SetLevel(Level level);
    static Level GetLevel();

private:
    std::string tag_;
    static Level global_level_;      // 默认 Level::Info
    static const char* LevelToStr(Level level);
    static void Sink(Level level, const char* tag, const char* message);
};

} // namespace utils
} // namespace atlas
```

### 3.4 平台适配方案

| 平台 | 宏条件 | 输出方式 | 链接依赖 |
|------|--------|----------|----------|
| Android | `__ANDROID__` | `__android_log_print()` → logcat | `-llog` |
| Apple (macOS / iOS) | `__APPLE__` | `os_log`（iOS）/ `std::printf`（macOS） | 无（macOS），`-los_log`（iOS） |
| Linux | `__linux__` | `std::printf` + 时间戳 | 无 |
| Windows | `_WIN32` | `OutputDebugStringA` + `std::printf` | 无 |

输出格式（非 Android 平台）：

```
2026-07-03 10:28:00 I Atlas::CpuBackend  ONNX Runtime initialized
2026-07-03 10:28:01 E Atlas::ModelLoader  Failed to load model: file not found
```

### 3.5 构建配置

库目标和测试目标分离到不同目录，遵循项目已有规范：

```python
# src/utils/BUILD（追加以下内容）
cc_library(
    name = "logger",
    srcs = ["logger.cc"],
    hdrs = ["logger.h"],
    visibility = ["//visibility:public"],
    linkopts = select({
        "@platforms//os:android": ["-llog"],
        "//conditions:default": [],
    }),
)
```

```python
# tests/utils/BUILD（新建）
cc_test(
    name = "logger_test",
    srcs = ["logger_test.cc"],
    deps = [
        "//src/utils:logger",
        "@googletest//:gtest_main",
    ],
)
```

### 3.6 集成方式

其他模块通过 `deps` 依赖 logger 即可使用：

```python
cc_library(
    name = "cpu_backend",
    deps = [
        "//src/utils:logger",
        # ...
    ],
)
```

代码中：

```cpp
#define LOG_TAG "Atlas::CpuBackend"
#include "utils/logger.h"

ErrorCode CpuBackend::Load(const std::string& model_path, ...) {
    ATLAS_LOGI("Loading model: %s", model_path.c_str());
    // ...
    if (failed) {
        ATLAS_LOGE("Model load failed: %s", error_msg);
        return ErrorCode::kModelLoadFailed;
    }
}
```

### 3.7 Release 编译裁剪

通过编译宏实现日志完全移除（零运行时开销）：

```cpp
#ifdef ATLAS_ENABLE_LOGGING
#  define ATLAS_LOGD(fmt, ...)  ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Debug, ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#  define ATLAS_LOGI(fmt, ...)  ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Info, ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#  define ATLAS_LOGW(fmt, ...)  ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Warn, ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#  define ATLAS_LOGE(fmt, ...)  ::atlas::utils::Logger::Log(::atlas::utils::Logger::Level::Error, ATLAS_LOG_TAG, fmt, ##__VA_ARGS__)
#else
#  define ATLAS_LOGD(fmt, ...)  ((void)0)
#  define ATLAS_LOGI(fmt, ...)  ((void)0)
#  define ATLAS_LOGW(fmt, ...)  ((void)0)
#  define ATLAS_LOGE(fmt, ...)  ((void)0)
#endif
```

Bazel 构建中通过 `defines` 控制：

```python
# Debug 构建开启日志
bazel build --define atlas_enable_logging=true //...

# Release 构建关闭日志
bazel build //...
```

---

## 四、不包含的内容

本次提案聚焦日志输出管理的**核心基础设施**，以下内容不在范围内：

| 不包含 | 原因 | 后续节奏 |
|--------|------|----------|
| 文件日志（写入磁盘） | 嵌入式/移动端场景无需求，增加复杂度 | 未来按需扩展 `FileSink` |
| 日志格式自定义（JSON 等） | 目前 printf 风格格式已足够 | 未来按需扩展 `Formatter` 策略 |
| 网络日志上报 | 不属于推理引擎职责 | 由上层应用处理 |
| 线程 ID 等元信息 | 增加运行时开销 | 未来通过编译宏按需开启 |
| 第三方日志库（spdlog 等） | 增加外部依赖，违背轻量化原则 | 不采用 |

---

## 五、受影响文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/utils/logger.h` | **新增** | 日志头文件（宏 + Logger 类声明） |
| `src/utils/logger.cc` | **新增** | 日志实现（平台适配） |
| `src/utils/BUILD` | 修改 | 添加 `logger` 库目标 |
| `tests/utils/BUILD` | **新增** | 新建测试目录及 `logger_test` 测试目标 |
| `tests/utils/logger_test.cc` | **新增** | 日志单元测试 |
| `docs/architecture.md` | 修改 | `src/utils/` 日志模块标注为「已实现」 |
| `docs/code_spec.md` | 修改 | 补充日志宏的具体使用示例 |
| `src/backend/cpu/cpu_backend.cc` | 修改（可选） | 集成 ATLAS_LOGx 宏 |
| `src/backend/snpe/snpe_backend_context_v2.cc` | 修改（可选） | 集成 ATLAS_LOGx 宏 |

### 5.1 不改写的文件

根据 `feature_spec.md` §4.5，以下文件中的日志相关内容属于历史记录，**不改写**：

- 各阶段文档（`phase{N}.md`）中已有的 ORT 日志级别设置描述
- 已有 proposal 文档中的日志相关内容

---

## 六、后续动作

- [x] 评审讨论（2026-07-03，通过）
- [x] 阶段归属：`→ docs/phase3.md`（补充：Feature 记录）
- [x] 实现：
  - [x] 新增 `src/utils/logger.h`
  - [x] 新增 `src/utils/logger.cc`
  - [x] 更新 `src/utils/BUILD` 添加 logger 库目标
  - [x] 新建 `tests/utils/BUILD` + `tests/utils/logger_test.cc`
  - [x] 更新 `docs/architecture.md` 日志模块状态
  - [x] 更新 `docs/code_spec.md` 补充日志宏使用规范
- [x] `bazel build //src/utils:logger` + `bazel test //tests/utils:logger_test` 通过
- [ ] 在 `src/backend/cpu/` 和 `src/backend/snpe/` 中添加日志集成（可选后续）
- [x] 实现完成，本文档状态为「已采纳」并归档
