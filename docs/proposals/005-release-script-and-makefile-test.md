# Proposal-005: 脚本编译输出与 Makefile 集成测试

> **提议日期**：2026-06-30
> **提议人**：pizzk <726676435@qq.com>
> **状态**：已采纳
> **类型**：模块级
> **关联**：`docs/phase6.md`（对外提供库及接口）

## 一、背景

### 1.1 现状

阶段六已经实现了完整的公共 API 层和 Bazel 公共库目标（`//src/public:atlas`），并提供了 `tools/install_atlas.sh` 安装脚本和 `atlas.pc` pkg-config 模板。外部 Bazel 项目可以通过 `http_archive` 或 `local_repository` 引入 Atlas 并依赖 `@atlas//src/public:atlas`。

### 1.2 存在问题

1. **缺少独立构建输出脚本**：`install_atlas.sh` 将产物安装到系统前缀（如 `/usr/local`），但不支持将编译产物输出到一个**可移植的 SDK 目录**（如 `./atlas-sdk/`），供外部非 Bazel 项目直接引用或打包分发。
2. **缺少跨平台构建支持**：`install_atlas.sh` 仅使用 `bazel build //src/public:atlas` 构建宿主平台产物，无法通过参数构建指定目标平台的 SDK（如交叉编译 linux_aarch64）。
3. **缺少非 Bazel 消费者的集成测试**：现有测试（`tests/public/`）和示例（`examples/two_model_pipeline/`）全部依赖 Bazel 构建系统。没有 Makefile 方式的集成测试，无法验证通过手动链接或 `pkg-config` 方式使用 Atlas SDK 的外部项目能否正常编译、链接和运行。

### 1.3 目标

- 提供 `tools/build_release.sh` 脚本，支持 `--platform` 参数指定目标平台，通过 Bazel `--config` 交叉编译，将产物（头文件、共享库、运行时依赖、pkg-config）输出到可移植的 SDK 目录（默认 `atlas-sdk/<platform>/`）。
- 新增 `examples/shared_library/` 示例项目，使用 Makefile 集成 SDK 头文件和共享库，实现端到端的接口调用测试，验证非 Bazel 场景下的可用性。

## 二、方案概要

### 2.1 脚本用法

```bash
# 构建当前宿主平台的 SDK
./tools/build_release.sh

# 构建指定平台的 SDK（平台名对应 //platforms:* 中定义的 config_setting）
./tools/build_release.sh --platform linux_aarch64

# 指定输出前缀（默认 ./atlas-sdk）
./tools/build_release.sh --platform macos_arm64 --prefix /tmp/my-sdk

# 列出当前项目支持的所有平台选项
./tools/build_release.sh --list-platforms

# 查看帮助
./tools/build_release.sh --help
```

### 2.2 核心流程

```
tools/build_release.sh --platform <name>
        │
        ▼  Bazel 构建
bazel build //src/public:atlas --config=linux_aarch64
        │
        ▼  输出到 SDK 目录
atlas-sdk/
└── <platform>/                      ← 平台子目录，如 linux-aarch64/
    ├── include/atlas/*.h            ← 公共头文件（跨平台通用）
    ├── lib/
    │   ├── libatlas.so.1.0.0        ← 版本化共享库
    │   ├── libatlas.so.1  → libatlas.so.1.0.0
    │   ├── libatlas.so     → libatlas.so.1
    │   ├── onnxruntime/
    │   │   └── libonnxruntime.so     ← 运行时依赖（平台相关）
    │   └── pkgconfig/atlas.pc       ← pkg-config 文件
    └── ...
        │
        ▼
examples/shared_library/            ← Makefile 集成示例
├── Makefile                        ← 通过 ATLAS_SDK 或 pkg-config 引用
├── main.cc                         ← 调用 AtlasRuntime / ModelHandle API
├── manifest.json                   ← 清单文件
├── identity_1x3x4x4.onnx          ← 静态 Identity 模型
└── README.md                       ← 使用说明
```

### 2.3 关键设计

#### `build_release.sh` 跨平台设计

| 机制 | 说明 |
|------|------|
| `--platform <name>` | 映射到 `bazel build --config=<name>`，`<name>` 对应 `//platforms:*` 中定义的 `config_setting` |
| 默认值 | 不指定时构建宿主平台（不传 `--config`，Bazel 自动探测） |
| 输出目录 | `atlas-sdk/<platform>/`，其中 `<platform>` 取自 `.bazelrc` 的 config 名，如 `macos-arm64`、`linux-aarch64` |
| 平台感知拷贝 | 根据目标平台确定共享库扩展名（`.so` / `.dylib`）和 ONNX Runtime 预编译包名 |
| `--list-platforms` | 列出所有支持的平台选项及对应的 Bazel config 名（从 `.bazelrc` 中解析 `build:<name>` 行或脚本内置列表） |
| `--help` | 查看完整帮助信息 |

#### `build_release.sh` 目录结构输出

```
atlas-sdk/
├── macos-arm64/           ← 宿主平台或 --platform=macos_arm64
│   ├── include/atlas/*.h
│   ├── lib/libatlas.1.0.0.dylib
│   └── lib/pkgconfig/atlas.pc
├── linux-aarch64/         ← --platform=linux_aarch64
│   ├── include/atlas/*.h
│   ├── lib/libatlas.so.1.0.0
│   └── lib/pkgconfig/atlas.pc
└── linux-x86_64/          ← --platform=linux_x86_64
    └── ...
```

> 头文件跨平台通用，脚本通过硬链接或符号链接共享同一份头文件目录，避免冗余。

#### Makefile 集成测试

- Makefile 通过 `ATLAS_SDK` 环境变量定位 SDK 路径，或回退到 `pkg-config` 自动检测。
- 使用**静态 ONNX 模型**（复用 `tests/backend/cpu/test_data/identity_1x3x4x4.onnx`），无需 Python 生成，零额外依赖。
- 支持三个目标：`make`（编译）、`make test`（运行验证）、`make clean`（清理）。
- Makefile 中通过 `uname -s` / `uname -m` 条件判断处理平台差异（`.so` / `.dylib` 扩展名、RPATH 设置方式等），确保在 macOS 和 Linux 上均能正常工作。

## 三、影响范围

| 项目 | 说明 |
|------|------|
| 新增 `tools/build_release.sh` | 脚本编译输出 SDK，支持 `--platform` 跨平台构建 |
| 新增 `examples/shared_library/` | Makefile 集成示例项目（含 `Makefile`、`main.cc`、`manifest.json`、`README.md`） |
| 引用现有 `tests/backend/cpu/test_data/identity_1x3x4x4.onnx` | 仅引用，不新增 |
| 相关文档 `docs/phase6.md` | 采纳后补充到阶段六文档 |

不涉及的内容：
- 不修改已有 Bazel BUILD 文件
- 不修改现有源代码
- 不引入新的三方依赖
- 不涉及 Windows 平台

**预估工作量**：小型（1 脚本 + 4 文件，约半天实现）

## 四、后续动作

- [ ] 评审讨论（记录日期与结论）
- [ ] 若采纳：补充到已有 `docs/phase6.md` 第十六章「脚本编译输出与集成测试」
- [ ] 若驳回：填写驳回理由