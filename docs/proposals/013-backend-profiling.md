# Proposal-013: 后端性能统计（Profiling）

> **提议日期**：2026-07-09
> **提议人**：pizzk <726676435@qq.com>
> **状态**：草案
> **类型**：模块级
> **关联**：`src/backend/base/i_backend.h`、`src/core/manifest_config.h`、`src/backend/snpe/snpe_backend_v1.cc`、`src/backend/snpe/snpe_backend_v2.cc`、`src/backend/cpu/cpu_backend.cc`、`benchmarks/infer_benchmark/benchmark_main.cc`

---

## 一、背景与动机

当前 Atlas 项目的**性能测量完全依赖外部基准测试工具**（`benchmarks/infer_benchmark/`），通过 `std::chrono` 包裹 `ModelHandle::Run()` 全链路计算耗时。这种方式存在三个不足：

| 问题 | 说明 |
|------|------|
| **粒度粗** | 只能统计完整推理链路耗时，无法区分 Load、Infer、Unload 三个阶段，更无法细分到数据预处理、模型执行、后处理等子步骤 |
| **需要独立程序** | 基准测试与主程序分离，生产环境无法按需开启性能采集 |
| **配置僵化** | 不支持运行时通过配置文件动态控制 profiling 的开关、粒度和输出目标 |

参考 **MediaPipe** 的 `CalculatorGraph::Options::enable_profiler` + `CalculatorProfile` 机制，它允许在配置文件中启用 profiler，自动采集每个 Calculator 的处理耗时，并输出到文件或流。Atlas 需要一个类似的、**可在 manifest 配置文件中动态启用**的后端性能统计系统。

---

## 二、方案设计

### 2.1 整体架构

在 `IBackend` 接口与具体后端实现之间插入一个可选的 **Profiling 装饰层**，不侵入现有后端代码：

```
ModelManager
  └→ ProfilingBackend (decorator, 可选的包装层)
        ├→ 计时 Load() / Infer() / Unload()
        ├→ 记录阶段性子步骤耗时
        └→ 输出到文件 / 控制台
              └→ 具体后端 (CpuBackend / SnpeBackend)
```

**设计原则**：
- **零侵入**：现有后端实现不感知 profiling，无需修改任何 `.cc` 文件
- **可插拔**：通过装饰器模式，只在 manifest 开启时才包装
- **可扩展**：支持的计时点（profiling point）通过标签系统添加，不影响核心逻辑

### 2.2 Manifest 配置

在 `ModelConfig::config`（`unordered_map<string, string>`）新增以下 Key，或支持在 manifest 顶层添加统一 profiling 配置：

#### 方式 A：模型级配置（推荐，与现有架构一致）

```json
{
  "models": [
    {
      "id": "face_detection",
      "backend": "snpe",
      "model_path": "models/face_detection.dlc",
      "config": {
        "runtime": "dsp",
        "profile_enabled": "true",
        "profile_output_path": "/data/logs/atlas_profile.csv",
        "profile_modules": "load,infer"
      }
    }
  ]
}
```

#### 方式 B：全局 profiling 配置（可选，用于统一管理所有模型）

```json
{
  "profile": {
    "enabled": true,
    "output_path": "/data/logs/atlas_profile.csv",
    "models": ["face_detection", "landmarks"],
    "modules": "load,infer,unload"
  },
  "models": [
    ...
  ]
}
```

优先采用**方式 A**，与现有设计一致（后端专属配置已通过 `config` 传递），同时在 `ManifestConfig` 顶部预留**可选的全局 profiling 节**作为补充。如果模型级和全局配置冲突，模型级优先级更高。

### 2.3 配置字段定义

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `profile_enabled` | bool | `false` | 是否开启 profiling |
| `profile_output_path` | string | `""`（stdout） | 统计结果输出文件路径；为空时输出到控制台 |
| `profile_modules` | string | `"load,infer"` | 要统计的阶段，逗号分隔；可选值：`load`、`infer`、`unload`、`all` |

### 2.4 Profiling 计时点

在 `ProfilingBackend` 装饰器中，对 `IBackend` 的每个接口方法添加以下计时点：

#### Load 阶段（`profile_modules` 含 `load`）

```
Load() 进入
  ├── [step] 上下文初始化 (Context::Init)
  ├── [step] 模型文件加载 (container open / session create)
  ├── [step] tensor 元数据构建 (BuildTensorInfos)
  ├── [step] 输入 ITensor 预分配
  └── [step] 装饰器自身开销
Load() 退出
```

#### Infer 阶段（`profile_modules` 含 `infer`）

```
Infer() 进入
  ├── [step] 输入数据拷贝（预分配 ITensor 填充）
  ├── [step] 模型执行 (snpe->execute / session->Run)
  └── [step] 输出数据组装（零拷贝包装）
Infer() 退出
```

#### Unload 阶段（`profile_modules` 含 `unload`）

```
Unload() 进入
  ├── [step] ITensor / buffer 清理
  ├── [step] SNPE / Session 析构
  └── [step] 容器 / 上下文释放
Unload() 退出
```

### 2.5 ProfilingBackend 装饰器实现

```cpp
// src/backend/base/profiling_backend.h

class ProfilingBackend : public IBackend {
 public:
    ProfilingBackend(std::unique_ptr<IBackend> inner,
                     const ProfileConfig& config);

    utils::ErrorCode Load(const std::string& model_path,
                          const core::ModelConfig& config,
                          IBackendContext* ctx) override;
    utils::ErrorCode Infer(const std::vector<utils::Tensor>& inputs,
                           std::vector<utils::Tensor>& outputs) override;
    void Unload() override;
    // ... 其他 IBackend 方法直接透传

 private:
    std::unique_ptr<IBackend> inner_;
    ProfileConfig profile_config_;
    // 累计统计
    std::vector<ProfileRecord> records_;
};
```

**关键实现要点**：

1. **计时**：使用 `std::chrono::steady_clock`，确保计时稳定不受系统时间跳变影响
2. **记录结构**：
   ```cpp
   struct ProfileRecord {
       std::string model_id;
       std::string phase;       // "load" / "infer" / "unload"
       std::string step;        // 子步骤名称
       int64_t     duration_us; // 耗时（微秒）
       int64_t     timestamp;   // 采集时间戳
   };
   ```
3. **输出格式**（CSV，易于后续分析）：
   ```csv
   model_id,phase,step,duration_us,timestamp
   face_detection,load,container_open,15234,1720500000000
   face_detection,load,build_tensor_info,892,1720500000015
   face_detection,infer,input_copy,1234,1720500001000
   face_detection,infer,execute,45678,1720500001023
   face_detection,infer,output_wrap,56,1720500001079
   ```
4. **刷新策略**：
   - 每条 `Infer()` 结束时立即写入（实时性要求高的场景）
   - 或在 `Unload()` / 析构时批量写入（减少 IO 开销）
   - 可配置缓冲区大小，到达阈值时自动 flush

### 2.6 BackendFactory 集成

`BackendFactory::Create()` 增加 profiling 包装逻辑：

```cpp
std::unique_ptr<IBackend> BackendFactory::Create(
    const std::string& name, const core::ModelConfig& config) {
    auto backend = CreateBackend(name);
    if (!backend) return nullptr;

    auto it = config.config.find("profile_enabled");
    if (it != config.config.end() && it->second == "true") {
        backend = std::make_unique<ProfilingBackend>(std::move(backend),
                                                      ParseProfileConfig(config));
    }
    return backend;
}
```

无需修改 `ModelManager` 或下游调用方。

### 2.7 与外部基准测试工具的关系

| 维度 | 外部 Benchmark（`benchmarks/infer_benchmark/`） | Profiling 系统（此提案） |
|------|-----------------------------------------------|------------------------|
| 定位 | 独立性能验收工具，CI 中使用 | 生产环境按需诊断工具 |
| 调用方式 | 独立可执行文件，单模型多次迭代 | 嵌入在 `ModelHandle::Run` 链路中 |
| 粒度 | 全链路一次计时 | 多阶段、多子步骤细粒度采集 |
| 输出 | P50/P95/P99/Mean/Throughput | 原始 CSV 数据，可后处理分析 |
| 开启方式 | 编译后运行 | 配置文件动态开启 |

两者互补，**不是替代关系**。

---

## 三、改动清单

| 文件 | 改动 |
|------|------|
| `src/backend/base/profiling_backend.h` | **新建**：`ProfilingBackend` 装饰器类声明 |
| `src/backend/base/profiling_backend.cc` | **新建**：`ProfilingBackend` 实现，含计时、记录、CSV 输出 |
| `src/backend/base/backend_factory.h` | 新增 `ParseProfileConfig()` 声明（可选） |
| `src/backend/base/backend_factory.cc` | `Create()` 中检测 `profile_enabled` 配置，条件性包装 `ProfilingBackend` |
| `src/core/manifest_config.h` | 新增 `ProfileConfig` 结构体（可选，或解析 inline 在 factory 中） |
| `manifest/` | 更新示例 manifest 添加 profiling 配置（可选） |

---

## 四、验证方案

1. **单元测试**：
   - 构造一个 `MockBackend`，包装 `ProfilingBackend`，验证五组 `Load`/`Infer` 调用后记录条数正确
   - 验证 CSV 输出的字段分隔符、换行符正确

2. **集成测试**：
   - 在内置模型上开启 profiling，确认输出文件生成且内容可解析
   - 关闭 profiling 时确认零额外开销（`ProfilingBackend` 不参与调用的概率）

3. **性能影响验证**：
   - 开启 profiling 后单次 `Infer()` 额外耗时 < 10μs（计时 + 写文件缓冲）
   - 关闭 profiling 时性能零损失

---

## 五、未解决的问题

1. **多线程安全**：`ProfilingBackend` 目前单线程设计，如果 `IBackend` 接口未来支持并发调用，需要加锁保护 `records_` 缓冲区
2. **日志文件轮转**：CSV 持续写入会无限增长，后续可增加文件大小阈值，自动轮转（如 `profile_max_size_mb` 配置）
3. **对接外部 tracing**：CSV 是基础格式，后续可考虑转换为 Chrome Trace Event Format（`.json`），在 `chrome://tracing` 中可视化
4. **装饰器透传**：`IBackend` 的所有纯查询方法（`GetInputInfo`、`GetOutputInfo`、`IsLoaded`、`Version`）直接透传给 `inner_`，不添加 profiling 埋点——这些方法的高频调用不应引入计时干扰
