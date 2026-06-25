# 阶段一实现方案：Bazel 环境 + 清单解析器 + 后端接口定义

## 一、目标与交付物

| 交付物 | 说明 |
|--------|------|
| Bazel 6.5 构建环境 | WORKSPACE + 三方依赖接入，支持本机与交叉编译 |
| 清单文件 JSON Schema | 定义并校验 manifest.json 格式 |
| `ManifestParser` 模块 | 解析清单，输出结构化 `ManifestConfig` |
| `IBackend` 接口 + `BackendFactory` | 后端抽象层骨架，无具体后端实现 |
| 基础类型定义 | `Tensor`、`DataType`、`ErrorCode` 等公共类型 |
| 单元测试 | 覆盖 ManifestParser 的正常/异常解析路径 |

---

## 二、清单文件格式设计

### 2.1 格式选型：JSON

| 对比项 | JSON (nlohmann/json) | YAML (yaml-cpp) |
|--------|----------------------|-----------------|
| 解析库 | header-only，Bazel 接入零成本 | 需编译静态库 |
| 可读性 | 一般，无注释支持 | 好，支持注释 |
| 边缘设备兼容 | 好 | 好 |
| Schema 校验 | 有 JSON Schema 标准工具链 | 无官方标准 |

**决策：采用 JSON 格式，使用 `nlohmann/json` 解析库。**
注释需求通过外部文档或 `_comment` 字段约定解决。

### 2.2 manifest.json 结构

```json
{
  "version": "1.0",
  "name": "my-vision-app",
  "models": [
    {
      "id": "detector",
      "name": "YOLOv8 Object Detector",
      "backend": "onnx",
      "model_path": "${MODEL_DIR}/yolov8n.onnx",
      "inputs": [
        {
          "name": "images",
          "shape": [1, 3, 640, 640],
          "dtype": "float32",
          "layout": "NCHW",
          "normalize": {
            "mean": [0.485, 0.456, 0.406],
            "std":  [0.229, 0.224, 0.225]
          }
        }
      ],
      "outputs": [
        {
          "name": "output0",
          "shape": [1, 84, 8400],
          "dtype": "float32"
        }
      ],
      "config": {
        "num_threads": "4",
        "execution_mode": "sequential"
      }
    }
  ]
}
```

### 2.3 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `version` | string | ✅ | 清单协议版本，用于兼容性检查 |
| `name` | string | ✅ | 应用/产品名称 |
| `models` | array | ✅ | 模型列表，至少一个 |
| `models[].id` | string | ✅ | 唯一标识，全局不重复 |
| `models[].backend` | string | ✅ | 运行后端名称（onnx/tensorrt/snpe/rknn） |
| `models[].model_path` | string | ✅ | 支持 `${ENV_VAR}` 环境变量替换 |
| `inputs[].shape` | int[] | ✅ | 静态形状；动态维度用 `-1` 表示 |
| `inputs[].dtype` | string | ✅ | float32 / float16 / int8 / uint8 / int32 |
| `inputs[].layout` | string | ❌ | NCHW / NHWC，默认 NCHW |
| `inputs[].normalize` | object | ❌ | 归一化参数，Pipeline 自动处理 |
| `models[].config` | object | ❌ | 后端专属 KV 配置，值统一为 string |

---

## 三、三方库选型与 Bazel 接入

### 3.1 依赖清单

| 库 | 版本 | 用途 | 引入方式 |
|----|------|------|----------|
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON 解析 | `http_archive`（header-only） |
| [GoogleTest](https://github.com/google/googletest) | 1.14.0 | 单元测试 | `http_archive` |

### 3.2 WORKSPACE 配置要点

```python
# WORKSPACE

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# nlohmann/json —— header-only，自定义 BUILD 文件
http_archive(
    name = "nlohmann_json",
    url = "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp",
    # 使用 single_header，提供自定义 BUILD
    build_file = "//third_party:nlohmann_json.BUILD",
    sha256 = "<sha256>",
)

# GoogleTest
http_archive(
    name = "googletest",
    url = "https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz",
    sha256 = "<sha256>",
    strip_prefix = "googletest-1.14.0",
)
```

```python
# third_party/nlohmann_json.BUILD
cc_library(
    name = "json",
    hdrs = ["json.hpp"],
    include_prefix = "nlohmann",
    visibility = ["//visibility:public"],
)
```

---

## 四、数据结构定义

### 4.1 公共类型（`src/utils/types.h`）

```cpp
namespace atlas {

// 数据类型枚举
enum class DataType {
    kUnknown = 0,
    kFloat32,
    kFloat16,
    kInt8,
    kUInt8,
    kInt32,
};

// 错误码：不使用异常，所有接口通过返回值传递状态
enum class ErrorCode {
    kOk = 0,
    kInvalidArgument,
    kFileNotFound,
    kParseError,
    kVersionMismatch,
    kBackendNotFound,
    kInferFailed,
    kNotInitialized,
};

// 归一化参数
struct NormalizeParams {
    std::vector<float> mean;
    std::vector<float> std;
};

// 张量元信息（来自清单描述）
struct TensorInfo {
    std::string name;
    std::vector<int> shape;   // -1 表示动态维度
    DataType dtype = DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
};

// 运行时张量（持有或借用内存）
struct Tensor {
    TensorInfo info;
    void*  data = nullptr;
    size_t byte_size = 0;
    bool   owns_data = false;  // true 时析构负责释放

    ~Tensor() {
        if (owns_data && data) {
            free(data);
            data = nullptr;
        }
    }
    // 禁止拷贝，仅允许移动
    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;
    Tensor(Tensor&&) noexcept = default;
    Tensor& operator=(Tensor&&) noexcept = default;
    Tensor() = default;
};

} // namespace atlas
```

### 4.2 清单配置结构（`src/core/manifest_config.h`）

```cpp
namespace atlas {

struct ModelConfig {
    std::string id;
    std::string name;
    std::string backend;
    std::string model_path;                              // 环境变量已展开
    std::vector<TensorInfo> inputs;
    std::vector<TensorInfo> outputs;
    std::unordered_map<std::string, std::string> config; // 后端专属配置
};

struct ManifestConfig {
    std::string version;
    std::string name;
    std::vector<ModelConfig> models;

    // 按 id 快速查找
    const ModelConfig* FindModel(const std::string& id) const;
};

} // namespace atlas
```

### 4.3 后端抽象接口（`src/backend/base/i_backend.h`）

```cpp
namespace atlas {

class IBackend {
public:
    virtual ~IBackend() = default;

    // 加载模型文件，config 为清单中的后端专属配置
    virtual ErrorCode Load(const std::string& model_path,
                           const ModelConfig& config) = 0;

    // 同步推理，inputs/outputs 均为调用方管理的 Tensor
    virtual ErrorCode Infer(const std::vector<Tensor>& inputs,
                            std::vector<Tensor>&       outputs) = 0;

    // 获取模型实际输入/输出信息（Load 后有效）
    virtual std::vector<TensorInfo> GetInputInfo()  const = 0;
    virtual std::vector<TensorInfo> GetOutputInfo() const = 0;

    // 卸载模型，释放后端资源
    virtual void Unload() = 0;

    // 后端是否已就绪
    virtual bool IsLoaded() const = 0;
};

} // namespace atlas
```

### 4.4 后端工厂（`src/backend/base/backend_factory.h`）

```cpp
namespace atlas {

// 后端创建函数类型
using BackendCreator = std::function<std::unique_ptr<IBackend>()>;

class BackendFactory {
public:
    static BackendFactory& Instance();

    // 注册后端（各后端在编译单元静态初始化时调用）
    void Register(const std::string& name, BackendCreator creator);

    // 创建后端实例，name 不存在返回 nullptr
    std::unique_ptr<IBackend> Create(const std::string& name) const;

    // 查询已注册的后端列表
    std::vector<std::string> ListBackends() const;

private:
    std::unordered_map<std::string, BackendCreator> creators_;
};

// 自动注册辅助宏（各后端实现文件末尾调用）
#define ATLAS_REGISTER_BACKEND(name, cls)                          \
    static bool _##cls##_registered = []() {                       \
        atlas::BackendFactory::Instance().Register(                \
            name, []() { return std::make_unique<cls>(); });       \
        return true;                                               \
    }()

} // namespace atlas
```

---

## 五、ManifestParser 实现要点

### 5.1 处理流程

```
读取文件
   │
   ▼
JSON 解析（nlohmann/json）
   │
   ▼
版本兼容性校验（major 版本必须匹配）
   │
   ▼
字段完整性校验（必填字段、枚举合法性）
   │
   ▼
model_path 环境变量展开（${VAR} → getenv(VAR)）
   │
   ▼
dtype / layout 字符串 → 枚举转换
   │
   ▼
输出 ManifestConfig
```

### 5.2 关键实现细节

- **环境变量展开**：正则或手动扫描 `${...}`，未定义环境变量返回 `kInvalidArgument`；
- **版本校验**：解析 `major.minor` 格式，major 不一致即报 `kVersionMismatch`；
- **id 唯一性**：解析完成后校验 model id 是否重复；
- **错误信息**：所有解析错误附带字段路径（如 `models[0].inputs[1].dtype`），方便排查。

---

## 六、目录结构（阶段一范围）

```
atlas/
├── WORKSPACE
├── BUILD                          # 顶层 BUILD
├── manifest/
│   ├── schema/
│   │   └── manifest.schema.json   # JSON Schema（可选，用于 IDE 校验）
│   └── examples/
│       └── example.json           # 示例清单文件
├── src/
│   ├── utils/
│   │   ├── types.h                # DataType, ErrorCode, Tensor, TensorInfo
│   │   └── BUILD
│   ├── core/
│   │   ├── manifest_config.h      # ManifestConfig, ModelConfig
│   │   ├── manifest_parser.h
│   │   ├── manifest_parser.cc
│   │   └── BUILD
│   └── backend/
│       └── base/
│           ├── i_backend.h
│           ├── backend_factory.h
│           ├── backend_factory.cc
│           └── BUILD
├── tests/
│   └── core/
│       ├── manifest_parser_test.cc
│       ├── test_data/             # 测试用清单文件（正常/异常）
│       └── BUILD
└── third_party/
    └── nlohmann_json.BUILD
```

---

## 七、单元测试覆盖点

| 测试用例 | 期望结果 |
|----------|----------|
| 解析完整合法的 manifest.json | 返回 `kOk`，字段值正确 |
| 缺少必填字段 `version` | 返回 `kParseError` |
| model id 重复 | 返回 `kInvalidArgument` |
| `dtype` 填写非法字符串 | 返回 `kParseError` |
| `model_path` 含 `${MODEL_DIR}` 且环境变量已设置 | 路径正确展开 |
| `model_path` 含未定义环境变量 | 返回 `kInvalidArgument` |
| major 版本不兼容（如框架 1.x vs 清单 2.x） | 返回 `kVersionMismatch` |
| 空 models 数组 | 返回 `kInvalidArgument` |
| BackendFactory 注册 + 创建 | 返回正确实例 |
| BackendFactory 创建未注册后端 | 返回 `nullptr` |

---

## 八、阶段一不包含的内容

- 任何具体后端实现（ONNX/TensorRT/RKNN/SNPE）
- Pipeline 预处理逻辑
- ModelManager 与 ModelPool
- 对外 Atlas API 封装
- 交叉编译工具链配置（放入阶段四随目标平台接入）
