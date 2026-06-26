# 阶段六实现方案：对外提供库及接口

> **文档版本**：1.0.1
> **对应代码版本**：v1.0.0
> **最后更新**：2026-06-26
> **状态**：已实现

## 一、目标与交付物

| 交付物 | 说明 |
|--------|------|
| `libatlas.so` / `libatlas.dylib` | 汇编全部核心模块的单一共享库，内置 CpuBackend 注册 |
| `libatlas.a`（可选） | 静态库变体，用于嵌入式 / 边缘设备无动态链接场景 |
| `include/atlas/` 公共头文件目录 | 对外暴露的稳定头文件集合，与内部实现头文件隔离 |
| `atlas_export.h` 符号导出宏 | 控制 ABI 可见性，仅导出公共符号 |
| `//src/public:atlas` Bazel 目标 | 统一的公共库目标，外部项目通过此目标依赖 |
| `atlas.bzl` 集成宏 | 简化外部 Bazel 项目引入 Atlas 的工作量 |
| `tools/install_atlas.sh` 安装脚本 | 将头文件、库文件、运行时依赖安装到指定前缀 |
| `atlas.pc` pkg-config 模板 | 供非 Bazel 项目（CMake / Makefile）集成使用 |
| 集成示例 `examples/two_model_pipeline`（改造为公共 API） | 演示仅通过公共 API 链接 Atlas 并运行双模型推理 |
| 集成指南文档 | README 中补充「作为依赖使用」章节 |

### 设计原则

1. **最小暴露面**：仅导出 `atlas::api` 与 `atlas::utils` 命名空间中的公共类型，内部模块（core / pipeline / backend 实现细节）符号全部隐藏。
2. **ABI 稳定优先**：公共头文件不暴露 ONNX Runtime、nlohmann/json 等三方库类型，PIMPL 模式隔离实现。
3. **零侵入**：不改变现有 `src/` 下的内部头文件结构与 include 路径，通过新增 `include/` 与 `src/public/` 层做桥接。
4. **单一库文件**：对外只交付一个 `libatlas.so` + 一组头文件 + 运行时依赖的 `libonnxruntime.so`，降低集成复杂度。

---

## 二、公共 API 边界定义

### 2.1 公开类型清单

以下类型 / 函数构成 Atlas 对外稳定的 API 契约，后续版本变更需遵循语义化版本（SemVer）：

| 层 | 符号 | 当前位置 | 说明 |
|----|------|----------|------|
| API | `atlas::api::AtlasRuntime` | `src/api/atlas_runtime.h` | 运行时门面，Init / GetModel / Release |
| API | `atlas::api::ModelHandle` | `src/api/model_handle.h` | 模型推理句柄，Run / GetInputInfo / GetOutputInfo |
| Utils | `atlas::utils::Tensor` | `src/utils/types.h` | 运行时张量，移动语义 |
| Utils | `atlas::utils::TensorInfo` | `src/utils/types.h` | 张量元信息 |
| Utils | `atlas::utils::DataType` | `src/utils/types.h` | 元素数据类型枚举 |
| Utils | `atlas::utils::ErrorCode` | `src/utils/types.h` | 错误码枚举 |
| Utils | `atlas::utils::ErrorCodeToString()` | `src/utils/types.h` | 错误码转字符串 |
| Utils | `atlas::utils::ElementByteSize()` | `src/utils/types.h` | 数据类型字节数 |
| Utils | `atlas::utils::ElementCount()` | `src/utils/types.h` | 形状元素总数 |
| Utils | `atlas::utils::VersionString()` | `src/utils/version.h` | 版本字符串 |
| Utils | `atlas::utils::kVersionMajor/Minor/Patch` | `src/utils/version.h` | 编译期版本常量 |

### 2.2 内部类型（不对外暴露）

以下类型虽然在 `//visibility:public` 目标中，但**不**纳入公共头文件，外部项目不应直接引用：

| 模块 | 符号 | 原因 |
|------|------|------|
| core | `ManifestParser` / `ManifestConfig` / `ModelConfig` / `ModelManager` / `ModelEntry` | 实现细节，由 `AtlasRuntime` 内部持有 |
| backend | `IBackend` / `IBackendContext` / `BackendFactory` / `CpuBackend` | 后端插件机制，外部仅需通过 manifest 声明 backend |
| pipeline | `Pipeline` / `IPipelineNode` / 各 Node 子类 | 预处理管线由 manifest 自动构建，无需外部直接操作 |
| backend | `ATLAS_REGISTER_BACKEND` / `ATLAS_REGISTER_BACKEND_CONTEXT` 宏 | 仅在编译自有后端扩展时使用，见阶段四 |

### 2.3 三方库类型隔离要求

当前 `AtlasRuntime` / `ModelHandle` 的头文件**不**直接包含三方库头文件（已满足），但 `CpuBackend` 的公开头文件包含了 `onnxruntime_cxx_api.h`。由于 CpuBackend 不纳入公共头文件集合，此问题不影响外部消费者。需在新文档中明确声明：

> **公共头文件中禁止出现任何三方库的 `#include`。** 后续新增公共类型若需引用三方库，必须使用前向声明或 PIMPL 模式隔离。

---

## 三、公共头文件布局

### 3.1 目录结构

```
atlas/
├── include/                       # 对外公共头文件根目录（新増）
│   └── atlas/
│       ├── atlas.h                # 汇总头文件，include 全部公共 API
│       ├── atlas_runtime.h        # AtlasRuntime 声明
│       ├── model_handle.h         # ModelHandle 声明
│       ├── types.h                # Tensor / TensorInfo / DataType / ErrorCode
│       ├── version.h              # 版本信息
│       └── atlas_export.h         # 符号导出宏（由 CMake/bazel 生成或手写）
├── src/
│   ├── api/                       # 现有实现，不变
│   ├── core/                      # 现有实现，不变
│   ├── backend/                   # 现有实现，不变
│   ├── pipeline/                  # 现有实现，不变
│   ├── utils/                     # 现有实现，不变
│   └── public/                    # 公共库桥接层（新增）
│       ├── BUILD                  # 定义 //src/public:atlas 目标
│       └── atlas_init.cc          # 强制链接已注册后端的锚点（保证 alwayslink 生效）
├── tools/
│   ├── install_atlas.sh           # 安装脚本
│   └── atlas.pc.in                # pkg-config 模板
└── ...
```

### 3.2 公共头文件内容

公共头文件采用**薄封装**策略：在 `include/atlas/` 下放置对外头文件，内容与 `src/` 下对应头文件基本一致，但：

1. **修改 include 路径**：内部互相引用改为 `atlas/xxx.h` 形式；
2. **添加导出宏**：类声明添加 `ATLAS_API` 前缀；
3. **移除内部依赖**：如 `atlas_runtime.h` 中对 `src/core/manifest_parser.h` 的依赖改为前向声明。

#### `include/atlas/atlas_export.h`

```cpp
#pragma once

// Symbol export / import macros for shared library ABI control.
//
// When building libatlas as a shared library, ATLAS_SHARED_LIBRARY is defined
// by the build system (Bazel defines / CMake add_compile_definitions), causing
// all ATLAS_API-decorated symbols to be exported with default visibility.
//
// Consumers of the shared library include this header without defining
// ATLAS_SHARED_LIBRARY, so ATLAS_API resolves to an import hint on Windows
// or a no-op on Linux/macOS (where import is implicit).
//
// All translation units are compiled with -fvisibility=hidden by default;
// only symbols marked ATLAS_API are exported.

#if defined(_WIN32)
  #if defined(ATLAS_SHARED_LIBRARY)
    #define ATLAS_API __declspec(dllexport)
  #else
    #define ATLAS_API __declspec(dllimport)
  #endif
#else
  #if defined(ATLAS_SHARED_LIBRARY)
    #define ATLAS_API __attribute__((visibility("default")))
  #else
    #define ATLAS_API
  #endif
#endif
```

#### `include/atlas/types.h`（节选关键差异）

```cpp
#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "atlas/atlas_export.h"

namespace atlas {
namespace utils {

// Supported tensor element data types.
enum class ATLAS_API DataType {
    kUnknown = 0,
    kFloat32,
    kFloat16,
    kInt8,
    kUInt8,
    kInt32,
};

// Error codes returned by all public API functions.
enum class ATLAS_API ErrorCode {
    kOk = 0,
    kInvalidArgument,
    kFileNotFound,
    kParseError,
    kVersionMismatch,
    kBackendNotFound,
    kInferFailed,
    kNotInitialized,
};

ATLAS_API const char* ErrorCodeToString(ErrorCode code);

// ... (TensorInfo, Tensor, helpers — Tensor struct gets ATLAS_API on methods)

struct TensorInfo {
    std::string name;
    std::vector<int> shape;
    DataType dtype = DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
};

struct ATLAS_API Tensor {
    // ... (same as current, methods decorated with ATLAS_API)
};

ATLAS_API size_t ElementByteSize(DataType dtype);
ATLAS_API size_t ElementCount(const std::vector<int>& shape);

}  // namespace utils
}  // namespace atlas
```

#### `include/atlas/atlas_runtime.h`

```cpp
#pragma once

#include <memory>
#include <string>

#include "atlas/atlas_export.h"
#include "atlas/model_handle.h"
#include "atlas/types.h"

namespace atlas {
namespace core { class ModelManager; class ManifestParser; }

namespace api {

class ATLAS_API AtlasRuntime {
 public:
    AtlasRuntime();
    ~AtlasRuntime();

    utils::ErrorCode Init(const std::string& manifest_path);
    ModelHandle GetModel(const std::string& model_id);
    void Release();

    bool IsInitialized() const;

 private:
    // PIMPL: internal parser and manager are hidden from the public header.
    std::unique_ptr<core::ManifestParser> parser_;
    std::unique_ptr<core::ModelManager>   manager_;
    bool                                  initialized_ = false;
};

}  // namespace api
}  // namespace atlas
```

> **关键变更**：`parser_` 由值成员改为 `unique_ptr`（PIMPL），公共头文件不再需要 `#include "src/core/manifest_parser.h"`，彻底隔离内部类型。`AtlasRuntime` 的 `.cc` 实现文件中完成 `ManifestParser` 的构造与析构。

#### `include/atlas/atlas.h` 汇总头文件

```cpp
#pragma once

// Convenience umbrella header — includes the entire Atlas public API.
// Consumers may either include this single header or include individual
// headers as needed.

#include "atlas/atlas_runtime.h"
#include "atlas/model_handle.h"
#include "atlas/types.h"
#include "atlas/version.h"
```

---

## 四、符号导出与 ABI 稳定性

### 4.1 编译选项

共享库构建时启用以下编译选项：

| 选项 | 作用 |
|------|------|
| `-fvisibility=hidden` | 默认隐藏所有符号，仅 `ATLAS_API` 标记的符号导出 |
| `-fvisibility-inlines-hidden` | 隐藏内联函数的弱符号，减少导出表体积 |
| `-DATLAS_SHARED_LIBRARY` | 触发 `ATLAS_API` 展开为 `__attribute__((visibility("default")))` |
| `-std=c++17` | 沿用现有标准 |

在 Bazel 中通过 `copts` 传入；`.bazelrc` 中已有 `--cxxopt=-std=c++17`。

### 4.2 后端注册符号保留

当前 `CpuBackend` 与 `CpuBackendContext` 使用 `ATLAS_REGISTER_BACKEND` 宏在匿名命名空间中注册，依赖 `alwayslink = 1` 确保链接器保留这些翻译单元。

在共享库场景下，链接器默认会丢弃未被直接引用的 `.o`。为确保 `libatlas.so` 内置的 CpuBackend 注册生效，采用**显式锚点**方案：

#### `src/public/atlas_init.cc`

```cpp
// Anchor translation unit that forces the linker to retain backend
// registration code when building libatlas.so / libatlas.a.
//
// The symbols referenced here are defined in alwayslink backend targets.
// Without this anchor, `ld -shared` may strip the static initializers
// that call BackendFactory::Register().

#include "src/backend/base/backend_factory.h"
#include "src/backend/cpu/cpu_backend.h"
#include "src/backend/cpu/cpu_backend_context.h"

namespace atlas {
namespace public_api {

// Returns the list of backends compiled into this library build.
// This function is called from AtlasRuntime::Init() to log available
// backends, and its mere existence forces the linker to pull in the
// cpu_backend / cpu_backend_context translation units.
void EnsureBackendsLinked() {
    auto& factory = ::atlas::backend::BackendFactory::Instance();
    (void)factory.ListBackends();
}

}  // namespace public_api
}  // namespace atlas
```

此文件加入 `//src/public:atlas` 的 `srcs`，`AtlasRuntime::Init()` 内部调用 `EnsureBackendsLinked()`，形成对 backend 符号的显式引用链。

### 4.3 SONAME 与版本

| 文件名 | SONAME | 说明 |
|--------|--------|------|
| `libatlas.so.1.0.0` | `libatlas.so.1` | 实际库文件，包含完整版本号 |
| `libatlas.so.1` → `libatlas.so.1.0.0` | — | SONAME 软链接，兼容补丁版本升级 |
| `libatlas.so` → `libatlas.so.1` | — | 开发软链接，供 `-latlas` 链接 |

macOS 对应为 `libatlas.1.0.0.dylib` / `libatlas.1.dylib` / `libatlas.dylib`。

Bazel 中通过 `cc_library` 的 `linkopts` 传入 `-Wl,-soname,libatlas.so.1`（Linux）或 `-Wl,-install_name,@rpath/libatlas.1.dylib`（macOS）。

> **讨论点**：初期版本是否需要完整的版本化 SONAME 方案？若短期内不承诺 ABI 兼容，可简化为单一 `libatlas.so`，待 1.1 版本再引入。**建议**：从 1.0.0 即引入 `.so.1` SONAME，避免后续迁移成本。

### 4.4 ABI 检查

引入 `tools/check_abi.sh` 脚本，基于 `abi-compliance-checker` 或 `libabigail` 在 CI 中对比公共头文件的 ABI 差异，防止意外破坏兼容性。此为可选项，不阻塞本阶段交付。

---

## 五、共享库构建方案

### 5.1 Bazel 目标定义

#### `src/public/BUILD`

```python
# Public library target that aggregates all Atlas modules into a single
# shared or static library for external consumption.
#
# External Bazel projects depend on //src/public:atlas.
# Internal targets (tests, examples) continue to depend on granular targets
# under src/api, src/core, etc.

cc_library(
    name = "atlas",
    srcs = [
        "atlas_init.cc",
    ],
    hdrs = glob(["include/atlas/*.h"]),
    # Public headers are exposed under the "atlas/" include prefix.
    # src/public/include is the include root; consumers write
    #   #include "atlas/atlas_runtime.h"
    strip_include_prefix = "include",
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-DATLAS_SHARED_LIBRARY",
    ],
    linkopts = select({
        "@bazel_tools//src/conditions:darwin": [
            "-Wl,-install_name,@rpath/libatlas.1.dylib",
        ],
        "//conditions:default": [
            "-Wl,-soname,libatlas.so.1",
        ],
    }),
    # Aggregate all implementation modules. alwayslink ensures backend
    # registration static initializers are retained.
    deps = [
        "//src/api:atlas_runtime",
        "//src/api:model_handle",
        "//src/backend/cpu:cpu_backend",
        "//src/backend/cpu:cpu_backend_context",
        "//src/core:manifest_config",
        "//src/core:manifest_parser",
        "//src/core:model_manager",
        "//src/pipeline:pipeline",
        "//src/utils:types",
        "//src/utils:version",
    ] + select({
        "@bazel_tools//src/conditions:darwin_arm64": ["@onnxruntime_macos_arm64//:onnxruntime"],
        "//conditions:default": ["@onnxruntime_linux_x86_64//:onnxruntime"],
    }),
    visibility = ["//visibility:public"],
)
```

### 5.2 共享库与静态库切换

Bazel 的 `cc_library` 默认根据消费目标的类型决定链接方式（`cc_binary` → 动态或静态，`cc_test` → 静态）。为显式产出 `.so` 文件，提供专用目标：

```python
# In src/public/BUILD — explicit shared library output

cc_binary(
    name = "libatlas.so",
    linkshared = True,
    linkstatic = False,
    deps = [":atlas"],
    # linkopts inherited from :atlas
)
```

构建命令：
```bash
# 共享库
bazel build //src/public:libatlas.so

# 静态库（Bazel 默认在 cc_library 上产出 .a，通过 cc_library 的 linkstatic=True）
bazel build --config=static //src/public:atlas  # 产出 libatlas.a
```

> **讨论点**：`cc_binary(linkshared=True)` 产出的 `.so` 不会自动应用 `:atlas` 的 `copts`。需要在 `libatlas.so` 目标上重复 `copts`，或改用 `cc_library` + `linkshared=True`（Bazel 7.x 支持的写法）。需确认当前 Bazel 版本行为。**备选方案**：使用 `genrule` 调用 `$(CC)` 显式链接，但可读性差，不推荐。

### 5.3 ONNX Runtime 运行时依赖

`libatlas.so` 动态链接 `libonnxruntime.so.1.17.3`。发布时需将 ONNX Runtime 库文件一并打包：

```
atlas-release/
├── lib/
│   ├── libatlas.so.1.0.0
│   ├── libatlas.so.1        → libatlas.so.1.0.0
│   ├── libatlas.so          → libatlas.so.1
│   └── onnxruntime/
│       ├── libonnxruntime.so.1.17.3
│       └── libonnxruntime.so.1   → libonnxruntime.so.1.17.3
├── include/atlas/
│   └── ... (公共头文件)
└── lib/pkgconfig/atlas.pc
```

运行时通过 `RPATH` 或 `LD_LIBRARY_PATH` 定位 `libonnxruntime.so`。建议在 `libatlas.so` 中设置 `RPATH="$ORIGIN/onnxruntime"`，使两者可整体部署到任意目录。

---

## 六、Bazel 依赖集成方案

### 6.1 方式一：http_archive（推荐用于正式版本）

外部项目在 `WORKSPACE` 中引入 Atlas：

```python
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# ---------------------------------------------------------------------------
# Atlas  v1.0.0
# ---------------------------------------------------------------------------
http_archive(
    name = "atlas",
    url = "https://github.com/<org>/atlas/archive/refs/tags/v1.0.0.tar.gz",
    sha256 = "<sha256-of-release-archive>",
    strip_prefix = "atlas-1.0.0",
)

# Atlas depends on ONNX Runtime prebuilt; this is fetched automatically
# by Atlas's own WORKSPACE when the consuming project runs bazel build.
```

外部项目的 `BUILD` 文件：

```python
cc_binary(
    name = "my_app",
    srcs = ["main.cc"],
    deps = [
        "@atlas//src/public:atlas",
    ],
)
```

外部代码中通过 `atlas/` 前缀引用头文件：

```cpp
#include "atlas/atlas.h"

int main() {
    atlas::api::AtlasRuntime rt;
    rt.Init("manifest.json");
    // ...
}
```

#### 依赖传递问题

Atlas 的 `WORKSPACE` 中声明的 `@onnxruntime_macos_arm64` / `@nlohmann_json` 不会自动传递给外部项目。有两种解决方案：

**方案 A（推荐）**：Atlas 提供一个 `atlas_deps.bzl` 宏，外部项目在 `WORKSPACE` 中调用：

```python
load("@atlas//:atlas_deps.bzl", "atlas_deps")

atlas_deps()
```

该宏内部执行 Atlas 所需的 `http_archive` 声明，确保依赖版本一致。

**方案 B**：使用 Bazel 7.x 的 `bzlmod`（module 依赖管理），Atlas 发布为 Bazel Central Registry 模块。此方案更现代但要求升级 Bazel 版本，可作为后续优化。

### 6.2 方式二：local_repository（开发期联调）

```python
# In external project's WORKSPACE
local_repository(
    name = "atlas",
    path = "/path/to/atlas",
)
```

适用于 Atlas 与消费项目在同一开发机上联合调试的场景。

### 6.3 `atlas.bzl` 便捷宏

为简化外部项目的 `BUILD` 文件，提供 `atlas.bzl`：

```python
# //src/public:atlas.bzl

def atlas_linkopts():
    """Returns linkopts needed when linking against libatlas."""
    return select({
        "@bazel_tools//src/conditions:darwin": [
            "-Wl,-rpath,@loader_path/../lib",
        ],
        "//conditions:default": [
            "-Wl,-rpath,$ORIGIN/../lib",
        ],
    })
```

外部项目使用：
```python
load("@atlas//src/public:atlas.bzl", "atlas_linkopts")

cc_binary(
    name = "my_app",
    srcs = ["main.cc"],
    deps = ["@atlas//src/public:atlas"],
    linkopts = atlas_linkopts(),
)
```

---

## 七、非 Bazel 项目集成方案

### 7.1 安装脚本

#### `tools/install_atlas.sh`

```bash
#!/usr/bin/env bash
# Installs Atlas headers, library, and runtime deps to a prefix.
# Usage: ./tools/install_atlas.sh /usr/local

set -euo pipefail

PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[1/4] Building libatlas.so ..."
bazel build //src/public:libatlas.so

echo "[2/4] Installing headers ..."
mkdir -p "${PREFIX}/include/atlas"
cp -r "${REPO_ROOT}/src/public/include/atlas/"*.h "${PREFIX}/include/atlas/"

echo "[3/4] Installing library ..."
mkdir -p "${PREFIX}/lib"
cp bazel-bin/src/public/libatlas.so "${PREFIX}/lib/libatlas.so.1.0.0"
ln -sf libatlas.so.1.0.0 "${PREFIX}/lib/libatlas.so.1"
ln -sf libatlas.so.1     "${PREFIX}/lib/libatlas.so"

# Copy ONNX Runtime shared library
ORT_LIB=$(find bazel-bin/external -name "libonnxruntime.so*" -o -name "libonnxruntime*.dylib*" | head -1)
if [[ -n "${ORT_LIB}" ]]; then
    mkdir -p "${PREFIX}/lib/atlas"
    cp "${ORT_LIB}" "${PREFIX}/lib/atlas/"
fi

echo "[4/4] Installing pkg-config ..."
mkdir -p "${PREFIX}/lib/pkgconfig"
sed "s|@PREFIX@|${PREFIX}|g" "${REPO_ROOT}/tools/atlas.pc.in" > "${PREFIX}/lib/pkgconfig/atlas.pc"

echo "Atlas installed to ${PREFIX}"
```

### 7.2 pkg-config 模板

#### `tools/atlas.pc.in`

```pkgconfig
prefix=@PREFIX@
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: Atlas
Description: Visual model runtime framework with multi-backend support
Version: 1.0.0
URL: https://github.com/<org>/atlas
Libs: -L${libdir} -latlas
Libs.private: -L${libdir}/atlas -lonnxruntime
Cflags: -I${includedir} -std=c++17
```

### 7.3 CMake 集成示例

外部 CMake 项目通过 `find_package(PkgConfig)` 使用：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(ATLAS REQUIRED atlas)

add_executable(my_app main.cc)
target_link_libraries(my_app ${ATLAS_LIBRARIES})
target_include_directories(my_app PRIVATE ${ATLAS_INCLUDE_DIRS})
target_compile_features(my_app PRIVATE cxx_std_17)
```

或直接指定路径（无 pkg-config 时）：

```cmake
find_library(ATLAS_LIB NAMES atlas PATHS /usr/local/lib)
find_path(ATLAS_INCLUDE NAMES atlas/atlas.h PATHS /usr/local/include)

add_executable(my_app main.cc)
target_link_libraries(my_app ${ATLAS_LIB})
target_include_directories(my_app PRIVATE ${ATLAS_INCLUDE})
target_compile_features(my_app PRIVATE cxx_std_17)
```

---

## 八、集成示例

> **【补充】** 实现阶段 | 2026-06-26 | 文档版本 1.0.0 → 1.0.1
> 原设计为独立 `examples/external_consumer/`，实现时合并到 `examples/two_model_pipeline/` 中，保留双模型验证场景的同时改用公共 API。

### 8.1 改造说明

`examples/two_model_pipeline/main.cc` 从依赖内部头文件改为仅使用公共 API：

**改造前（内部头文件）：**
```cpp
#include "src/api/atlas_runtime.h"
#include "src/api/model_handle.h"
#include "src/backend/cpu/cpu_backend.h"
#include "src/backend/cpu/cpu_backend_context.h"
#include "src/utils/types.h"
#include "src/utils/version.h"
```

**改造后（公共 API）：**
```cpp
#include "atlas/atlas.h"
```

**BUILD 文件改造：**
```python
# examples/two_model_pipeline/BUILD
# This example uses ONLY the public //src/public:atlas target,
# demonstrating how an external project would consume Atlas.
# No internal headers (src/...) are referenced.

cc_binary(
    name = "two_model_pipeline",
    srcs = ["main.cc"],
    data = ["manifest.json"],
    deps = [
        "//src/public:atlas",
    ],
)
```

> **验证点**：该示例**不**直接依赖 `//src/api`、`//src/backend/cpu` 等内部目标，证明公共库目标已自包含全部所需实现。同时保留了双模型（detector + classifier）推理验证，覆盖 eager/lazy 加载策略与共享 BackendContext 场景。

---

## 九、现有代码改造点

为支持上述方案，需对现有代码做以下最小化改造：

| 改造项 | 文件 | 内容 |
|--------|------|------|
| PIMPL 化 `AtlasRuntime` | `src/api/atlas_runtime.h` / `.cc` | `parser_` 改为 `unique_ptr<ManifestParser>`，公共头不再 include `manifest_parser.h` |
| 新增公共头文件目录 | `src/public/include/atlas/` | 创建 `types.h`、`atlas_runtime.h`、`model_handle.h`、`version.h`、`atlas.h`、`atlas_export.h` |
| 新增 `atlas_init.cc` | `src/public/atlas_init.cc` | 后端链接锚点 |
| 新增 `BUILD` | `src/public/BUILD` | 定义 `:atlas` 与 `:libatlas.so` 目标 |
| 新增 `atlas.bzl` | `src/public/atlas.bzl` | 便捷宏 |
| 新增 `atlas_deps.bzl` | 仓库根 | 依赖传递宏 |
| 新增安装脚本 | `tools/install_atlas.sh`、`tools/atlas.pc.in` | 打包安装 |
| `AtlasRuntime::Init` 调用 `EnsureBackendsLinked()` | `src/api/atlas_runtime.cc` | 确保后端注册生效 |

> **注意**：内部 `src/api/atlas_runtime.h` 保持不变（仍可直接被 tests / examples 引用），公共头文件 `include/atlas/atlas_runtime.h` 是独立的对外版本。两份头文件通过 `src/public/BUILD` 的 `deps` 关联到同一份 `.cc` 实现。后续如需统一，可通过 Feature 提议将内部头文件迁移至 `include/atlas/`。

---

## 十、目录结构总览（阶段六新增部分）

```
atlas/
├── include/                              # (保留为空或软链，实际头文件在 src/public/include)
├── src/
│   └── public/                           # 新增：公共库桥接层
│       ├── BUILD
│       ├── atlas.bzl                     # 便捷宏
│       ├── atlas_init.cc                 # 后端链接锚点
│       └── include/
│           └── atlas/
│               ├── atlas.h
│               ├── atlas_export.h
│               ├── atlas_runtime.h
│               ├── model_handle.h
│               ├── types.h
│               └── version.h
├── tools/
│   ├── install_atlas.sh                  # 安装脚本
│   ├── atlas.pc.in                       # pkg-config 模板
│   └── check_abi.sh                      # ABI 检查（可选）
├── atlas_deps.bzl                        # 外部项目依赖传递宏
├── examples/
│   └── two_model_pipeline/             # 已改造为公共 API（双模型推理 + 公共库验证）
│       ├── BUILD
│       ├── README.md
│       ├── manifest.json
│       ├── gen_models.py
│       └── main.cc
└── ...
```

---

## 十一、单元测试覆盖点

| 测试文件 | 覆盖点 |
|----------|--------|
| `tests/public/atlas_lib_test.cc` | 验证 `//src/public:atlas` 目标可被独立链接，`AtlasRuntime::Init` 后 `BackendFactory` 中存在 `"cpu"` 后端（验证 alwayslink 锚点生效） |
| `tests/public/atlas_export_test.cc` | 验证公共头文件可被仅 include `atlas/atlas.h` 编译通过，无遗漏的类型引用 |
| `tests/public/abi_smoke_test.cc` | 基础 ABI 烟雾测试：sizeof 关键类型、枚举值不变（防止意外修改布局） |
| `examples/two_model_pipeline`（公共 API 改造后） | 作为集成测试，验证 `bazel build //examples/two_model_pipeline` 通过且运行正常 |

---

## 十二、发布 Checklist（阶段六）

| 项目 | 完成条件 |
|------|---------|
| `bazel build //src/public:libatlas.so` 成功 | 产出可加载的共享库 |
| `nm -D libatlas.so \| grep atlas` 验证符号导出 | 仅 `ATLAS_API` 标记的符号可见，内部符号隐藏 |
| `ldd libatlas.so`（Linux）/ `otool -L libatlas.dylib`（macOS） | 仅依赖 `libonnxruntime` 与系统库 |
| `examples/two_model_pipeline` 构建运行通过 | 证明公共 API 自包含（仅依赖 `//src/public:atlas`） |
| `tools/install_atlas.sh` 执行成功 | 头文件 / 库文件 / pkg-config 安装到指定前缀 |
| pkg-config 验证 `pkg-config --cflags --libs atlas` | 输出正确的编译 / 链接选项 |
| 公共头文件无 `src/` 路径 include | `grep -r 'src/' include/` 无结果 |

---

## 十三、阶段六不包含的内容

- **C 语言绑定接口**：C API 包装（`atlas_c_api.h`）留待后续 Feature 提议，为 Python / Go / Rust 等 FFI 绑定做准备。
- **Bazel bzlmod 发布**：Bazel Central Registry 模块发布需升级 Bazel 至 7.x，暂不涉及。
- **多后端动态加载**：运行时通过 `dlopen` 加载独立后端插件 `.so`（如 `libatlas_backend_tensorrt.so`）的机制留待阶段四后端扩展时设计。
- **Windows 平台支持**：当前 `select()` 仅覆盖 macOS arm64 与 Linux x86_64，Windows `.dll` 构建不在本阶段范围。
- **API 稳定性自动检查 CI**：`abi-compliance-checker` 集成为可选项，不阻塞交付。
- **内部头文件迁移**：`src/api/*.h` 与 `src/utils/types.h` 仍保留原位供内部使用，公共头文件为独立副本。统一为单一头文件树留待后续 Feature 提议。
- **模型加密 / License 校验**：商业发布相关机制不在技术方案范围。

---

## 十四、待讨论问题

以下问题需在开发者评审中确认后进入实现：

1. **共享库 SONAME 策略**：从 1.0.0 即引入 `libatlas.so.1` 版本化 SONAME，还是初期简化为 `libatlas.so`？
   - **建议**：引入 `.so.1`，避免后续迁移成本。

2. **公共头文件与内部头文件的关系**：维护两份头文件（公共 `include/atlas/` + 内部 `src/api/`）还是直接将内部头文件迁移为公共头文件？
   - **建议**：本阶段先维护两份，验证公共 API 边界稳定后，在阶段七统一为单一头文件树。

3. **`cc_binary(linkshared=True)` vs `cc_library(linkshared=True)`**：需根据 Bazel 版本确认哪种方式能正确传递 `copts` 与 `alwayslink`。
   - **建议**：实现阶段先验证 `cc_library` + `linkshared=True`（Bazel 7.x），若不可行回退到 `cc_binary` 并手动复制 `copts`。

4. **ONNX Runtime 库的分发方式**：随 Atlas 发布包内置 `libonnxruntime.so`，还是要求用户自行安装？
   - **建议**：内置分发，降低集成门槛；同时在文档中标注可替换为系统安装版本。

5. **`atlas_deps.bzl` 是否随 release archive 提供**：外部项目通过 `http_archive` 引入 Atlas 时，`WORKSPACE` 中的 `http_archive` 声明是否应改为 `atlas_deps()` 调用？
   - **建议**：提供 `atlas_deps.bzl`，外部项目 `WORKSPACE` 中 `load("@atlas//:atlas_deps.bzl", "atlas_deps")` + `atlas_deps()` 一行完成依赖拉取。

6. **静态库是否在本阶段交付**：嵌入式场景可能需要 `libatlas.a`，但 Bazel 产出的静态库会包含三方库 `.o`，体积与许可需评估。
   - **建议**：本阶段仅交付共享库，静态库作为可选项在实现时验证可行性。

---

## 十五、实现与设计的差异说明

> **【补充】** 实现阶段 | 2026-06-26 | 文档版本 1.0.0 → 1.0.1
> 以下差异在实现过程中确认，非原始设计的一部分。

### 15.1 SONAME / install_name 未在 cc_library 中设置

**设计**：在 `cc_library` 的 `linkopts` 中设置 `-Wl,-soname,libatlas.so.1` / `-Wl,-install_name,@rpath/libatlas.1.dylib`。

**实际**：移除了 `linkopts` 中的 SONAME 设置。原因：自定义 `install_name` 会破坏 Bazel 沙箱的 RPATH 解析，导致测试运行时找不到动态库。SONAME 改由安装脚本 (`tools/install_atlas.sh`) 在发布时通过 `install_name_tool` 设置。

### 15.2 公共头文件中的 ATLAS_API 装饰简化

**设计**：所有公共类和函数添加 `ATLAS_API` 宏装饰。

**实际**：`AtlasRuntime`、`ModelHandle`、`Tensor` 类及 `DataType`/`ErrorCode` 枚举未添加 `ATLAS_API`。原因：这些类型的实现位于 deps 的 `.cc` 文件中（如 `src/api/atlas_runtime.cc`），编译时未定义 `ATLAS_SHARED_LIBRARY`，`ATLAS_API` 为空宏。在类声明上添加 `ATLAS_API` 不会产生实际导出效果，反而可能造成 ABI 误导。

内联辅助函数（`ErrorCodeToString`、`ElementByteSize`、`ElementCount`）在公共头文件中保持 `inline` 实现（与内部 `types.h` 一致），不使用 `ATLAS_API` 导出。这避免了符号解析问题。

`atlas_export.h` 仍然保留，供后续需要显式控制符号导出的场景使用。

### 15.3 cc_binary(linkshared=True) 目标移除

**设计**：提供 `cc_binary(name = "libatlas.so", linkshared = True)` 专用目标。

**实际**：移除了该目标。原因：`cc_library` 已经产出共享库（macOS 上为 `libatlas.dylib`），`cc_binary` 与其输出路径冲突。直接使用 `bazel build //src/public:atlas` 即可获取共享库产物。
