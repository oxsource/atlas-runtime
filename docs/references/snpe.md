# SNPE SDK 外部引用参考

> 记录 SNPE SDK 的安装路径、版本信息、头文件结构及关键差异，供开发调试时快速查阅。

---

## 一、SDK 安装路径

### 1.1 v1 (SNPE 1.50.0)

```
/opt/qcom/sdk/snpe-1.50.0.2622/
├── include/
│   └── zdl/
│       ├── SNPE/            # SNPE 主头文件（SNPE.hpp, SNPEFactory.hpp, SNPEBuilder.hpp ...）
│       ├── DlSystem/        # 基础类型（DlEnums.hpp, DlError.hpp, ITensor.hpp, IUserBuffer.hpp ...）
│       ├── DlContainer/     # 模型容器（IDlContainer.hpp）
│       └── ...
├── lib/
└── ...
```

- **命名空间**: `zdl::DlSystem`, `zdl::SNPE`, `zdl::DlContainer`（带 `zdl::` 前缀）
- **错误函数声明**: `DlSystem/DlError.hpp`，自由函数 `zdl::DlSystem::getLastErrorString()`
- **注意**: v1 中 `DlError.hpp` **不被其他头文件** 传递包含，使用 `getLastErrorString()` 时必须显式 `#include "DlSystem/DlError.hpp"`

### 1.2 v2 (QAIRT 2.21.0)

```
/opt/qcom/aistack/qairt/2.21.0.240401/
├── include/
│   └── SNPE/
│       ├── SNPE/            # SNPE 主头文件（SNPE.hpp, SNPEFactory.hpp, SNPEBuilder.hpp ...）
│       ├── DlSystem/        # 基础类型（DlEnums.hpp, DlError.hpp, ITensor.hpp, IUserBuffer.hpp ...）
│       ├── DlContainer/     # 模型容器（IDlContainer.hpp）
│       └── ...
├── lib/
└── ...
```

- **命名空间**: `DlSystem`, `SNPE`, `DlContainer`（无 `zdl::` 前缀）
- **错误函数声明**: `DlSystem/DlError.hpp`，同时提供 `DlSystem::` 和 `zdl::DlSystem::` 两个命名空间的别名
- **注意**: v2 中 `DlError.hpp` **被多个头文件传递包含**（如 `StringList.hpp`, `TensorMap.hpp`, `UserBufferMap.hpp` 等均包含 `DlError.hpp`），因此无需显式 include

---

## 二、版本信息

| 属性 | v1 (SNPE) | v2 (QAIRT) |
|------|-----------|------------|
| 产品 | SNPE SDK | QAIRT (Qualcomm AI Runtime) |
| 版本 | 1.50.0.2622 | 2.21.0.240401 |
| 安装路径 | `/opt/qcom/sdk/snpe-1.50.0.2622/` | `/opt/qcom/aistack/qairt/2.21.0.240401/` |
| 头文件根目录 | `include/zdl/` | `include/SNPE/` |
| 命名空间前缀 | `zdl::` | 无前缀（兼容别名 `zdl::`） |
| Bazel 仓库 | `@snpe_sdk` | `@snpe_sdk`（通过 `SNPE_MAJOR` 区分） |

---

## 三、关键头文件差异

### 3.1 `DlError.hpp` 传递包含链

| SDK | 传递包含 `DlError.hpp` 的头文件 |
|-----|--------------------------------|
| v1 | **无** — 需显式 include |
| v2 | `StringList.hpp`, `TensorMap.hpp`, `TensorShapeMap.hpp`, `UserBufferMap.hpp`, `UserMemoryMap.hpp` |

### 3.2 `getLastErrorString()` 声明位置

| SDK | 命名空间 | 声明文件 |
|-----|----------|----------|
| v1 | `zdl::DlSystem` | `DlSystem/DlError.hpp`（自由函数） |
| v2 | `DlSystem` + `zdl::DlSystem` 别名 | `DlSystem/DlError.hpp`（`inline` 函数，内部调用 C API `Snpe_ErrorCode_GetLastErrorString`） |

---

## 四、相关文件

| 文件 | 说明 |
|------|------|
| `src/backend/snpe/snpe_backend_v1.cc` | SNPE v1 后端实现（`zdl::` 命名空间） |
| `src/backend/snpe/snpe_backend_v2.cc` | SNPE v2 后端实现（无前缀命名空间） |
| `src/backend/snpe/snpe_backend_context_v1.cc` | v1 后端上下文 |
| `src/backend/snpe/snpe_backend_context_v2.cc` | v2 后端上下文 |
| `third_party/snpe/defs.bzl` | SNPE Bazel 构建定义，按 `SNPE_MAJOR` 选择 v1/v2 源码 |
| `third_party/snpe/snpe_v1.BUILD` | SNPE v1 的 Bazel BUILD 配置 |
| `third_party/snpe/snpe_v2.BUILD` | SNPE v2 的 Bazel BUILD 配置 |

---

## 五、常见问题

### 5.1 编译报错 `no member named 'getLastErrorString' in namespace 'zdl::DlSystem'`

**原因**: v1 SDK 的 `DlError.hpp` 未被传递包含，需显式 include。

**修复**: 在源文件顶部添加：

```cpp
#include "DlSystem/DlError.hpp"
```

该问题仅在 v1 后端中出现，v2 后端因其他头文件传递包含 `DlError.hpp` 而不受影响。