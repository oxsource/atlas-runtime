# 009: 后端独立版本号

## 动机

`atlas::utils::version.h` 提供的是 SDK 整体版本号。不同项目/团队可能独立维护不同后端（CPU/SNPE/其他），需要知道当前使用的具体后端版本号和底层推理库版本号。

## 方案

在 `IBackend` 新增纯虚函数 `Version()`，仅返回后端底层库版本号字符串。

| Backend | Library | Version | 示例 |
|---------|---------|---------|------|
| CpuBackend | onnxruntime | 运行时 API `Ort::GetVersionString()` | `1.17.3` |
| SnpeBackend (v1) | snpe | 运行时 API `zdl::SNPE::SNPEFactory::getLibraryVersion()` | `1.50.0.2622` |
| SnpeBackend (v2) | snpe | 运行时 API `SNPE::SNPEFactory::getLibraryVersion()` | `2.21.0.240401` |
| SnpeBackend (stub) | snpe | 无 SNPE SDK，固定返回 | `stub` |

## 改动文件

| # | 文件 | 改动 |
|---|------|------|
| 1 | `src/backend/base/i_backend.h` | 新增纯虚函数 `Version()` |
| 2 | `src/backend/cpu/cpu_backend.h` | 声明 `Version()` override |
| 3 | `src/backend/cpu/cpu_backend.cc` | 实现：返回 `Ort::GetVersionString()` |
| 4 | `src/backend/snpe/snpe_backend.h` | 声明 `Version()` override |
| 5 | `src/backend/snpe/snpe_backend_v1.cc` | 实现：返回 `zdl::SNPE::SNPEFactory::getLibraryVersion()` |
| 6 | `src/backend/snpe/snpe_backend_v2.cc` | 实现：返回 `SNPE::SNPEFactory::getLibraryVersion()` |
| 7 | `src/backend/snpe/snpe_backend_stub.cc` | 实现：返回 `stub` |

## 接口声明

```cpp
// Returns the backend library version string.
// Example: "1.17.3".
virtual std::string Version() const = 0;
```

## 状态

已采纳（2026-07-06）