# Third-Party 依赖管理规范

本文档定义 Atlas 项目中第三方依赖（third-party）在 Bazel 下的组织、复用与平台差异管理方式。

---

## 一、目标

统一第三方依赖接入方式，避免以下常见问题：

- 各模块重复书写同一组 `select({...})` 平台映射；
- 同一依赖在不同 BUILD 文件中出现不一致的 repo 选择；
- 依赖配置散落在业务目录，难以复用与对外暴露；
- 新增平台时改动点过多，容易漏改。

---

## 二、核心原则

### 2.1 平台差异统一下沉到 defs

所有与平台相关的依赖映射（例如 macOS/Linux/Android 的仓库切换）统一收敛到 `third_party/<dep>/defs.bzl`，业务 BUILD 文件只通过 `load()` 调用函数。

**推荐：**

- `onnxruntime_deps()`
- `snpe_sdk_deps()`
- `snpe_platform_defines()`

**禁止：**

- 在多个业务 BUILD 文件中复制同一段 `select({...})`。

### 2.2 每个依赖一个独立目录

每个第三方依赖使用单独目录维护：

```
third_party/
	onnxruntime/
	snpe/
	nlohmann_json/
```

目录内集中放置该依赖的 BUILD 模板、repo 规则与复用 defs。

### 2.3 业务模块只消费接口，不承载规则细节

业务模块（例如 `src/backend/*`、`src/public/*`）只负责声明“需要什么依赖”，不负责定义“平台如何选择依赖”。

---

## 三、目录与文件约定

每个依赖目录建议包含：

- `defs.bzl`：可复用函数（平台依赖、宏、通用 defines）
- `*.BUILD`：版本/平台对应的 BUILD 模板文件
- `*_repo.bzl`（可选）：repository_rule 与外部仓库装配逻辑
- `README.md`（可选）：依赖版本、升级步骤、验证命令

示例：

```
third_party/snpe/
	defs.bzl
	snpe_repo.bzl
	snpe_v1.BUILD
	snpe_v2.BUILD

third_party/onnxruntime/
	defs.bzl
	onnxruntime.BUILD
	onnxruntime_android_arm64.BUILD
	onnxruntime_android_x86_64.BUILD
```

---

## 四、defs 设计约定

### 4.1 函数粒度

优先返回“最小可组合单元”，避免大而全宏：

- 依赖列表函数：`*_deps()`
- 编译宏函数：`*_defines()`
- 源文件选择函数：`*_srcs()`

### 4.2 函数命名

使用 `<dep>_<kind>()` 命名，保持可读性和可搜索性：

- `onnxruntime_deps()`
- `snpe_sdk_deps()`
- `snpe_backend_srcs()`

### 4.3 返回值类型

- 依赖函数返回 `list` 或 `select({...})`（最终可直接拼接到 `deps`）。
- defines 函数返回 `list` 或 `select({...})`（可直接赋给 `defines`）。
- src 函数返回 `list` 或 `select({...})`（可直接赋给 `srcs`）。

---

## 五、业务 BUILD 使用方式

业务 BUILD 统一使用 `load()` 接入：

```starlark
load("//third_party/onnxruntime:defs.bzl", "onnxruntime_deps")
load("//third_party/snpe:defs.bzl", "snpe_sdk_deps")

cc_library(
		name = "example_target",
		deps = [
				"//src/utils:types",
		] + onnxruntime_deps() + snpe_sdk_deps(),
)
```

注意：

- 优先使用仓库内绝对 label（`//third_party/...`），避免相对路径跨目录漂移；
- 业务 BUILD 中禁止再写同义 `select`。

---

## 六、新增第三方依赖流程

1. 在 `third_party/<dep>/` 新建目录。
2. 添加 BUILD 模板与 repo 规则（如需要）。
3. 在 `defs.bzl` 提供统一复用函数（至少提供 `*_deps()`）。
4. 在 `WORKSPACE` 或依赖装配入口中接入新仓库。
5. 在业务 BUILD 中替换原始 `select` 为 `load + 函数调用`。
6. 运行构建回归：
	 - `bazel build //...`（或至少覆盖受影响目标）

---

## 七、迁移与治理规则

### 7.1 迁移顺序

- 先新增 `defs.bzl`，再逐个模块替换调用点；
- 替换完成后删除旧的重复局部定义。

### 7.2 代码评审检查项

- 是否在业务 BUILD 中新增了重复平台 `select`；
- 是否复用了现有 `third_party/<dep>/defs.bzl` 接口；
- 同一依赖的映射是否只有一个权威来源。

### 7.3 外部复用约束

如需外部仓库复用，请将 `third_party/<dep>/defs.bzl` 视为稳定接口：

- 非必要不改函数名；
- 参数与返回语义保持兼容；
- 破坏性改动需配套迁移说明。

---

## 八、当前项目落地项

- SNPE 平台差异与 SDK 依赖：统一收敛于 `third_party/snpe/defs.bzl`
- ONNX Runtime 平台依赖选择：统一收敛于 `third_party/onnxruntime/defs.bzl`

后续新增模块应直接复用上述 defs，不再复制同类 `select` 逻辑。

