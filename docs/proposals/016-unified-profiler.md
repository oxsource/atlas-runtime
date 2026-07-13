# Proposal-016: 统一 Profiler 设计

> **提议日期**：2026-07-10
> **提议人**：pizzk <726676435@qq.com>
> **状态**：草案
> **类型**：重构级
> **关联**：`docs/proposals/013-backend-profiling.md`、`src/profiler/profiler.h`、`src/profiler/profiler.cc`、`src/backend/base/profiling_backend.h`、`src/backend/base/profiling_backend.cc`、`src/api/model_handle.cc`、`src/core/model_manager.h`、`src/core/model_manager.cc`

---

## 一、背景与动机

### 1.1 现状

当前 Atlas 的 profiling 功能由两个独立的代码路径维护，写入**同一个 CSV 文件**：

| 路径 | 位置 | 文件管理 | 写入时机 |
|------|------|----------|----------|
| **ProfilingBackend** | `src/backend/base/profiling_backend.cc` | `Flush()` 内 `fopen`/`fclose` | Load/Infer/Unload 完成后缓冲，Unload/析构时批量 flush |
 | **Pipeline profiling** | `src/api/model_handle.cc` 匿名空间 | 每个 `Run()` 调用 `fopen`/`fclose` | 每个 `Run()` 即时写入 |

### 1.3 后续优化

在首次实现后，发现了以下问题并进行了修复：

1. **forward 步骤重复记录**：`ModelHandle::Run()` 和 `ProfilingBackend::Infer()` 都在记录 `infer/forward`，导致每个 model 的 forward 计时被写入两次。修复方案：移除 `ModelHandle::Run()` 中的 forward 计时，统一由 `ProfilingBackend` 负责。

2. **日志时间顺序混乱**：`ModelHandle::Run()` 使用 `Record()` 即时写入，而 `ProfilingBackend` 使用 `BufferRecord()` 延迟刷新，导致同一 model 的 input_pipeline/output_pipeline 排在 forward 前面。修复方案：将所有方法统一为 `Push()`，使用**共享静态缓冲区** `s_records_`，按 push 顺序写入 CSV，保持时间顺序。

3. **API 命名统一**：`BufferRecord()` → `Push()`，`Record()` 方法移除，`Flush()` → `FlushAll()`（静态方法）。引入 `s_mutex_` 保护共享缓冲区，`s_max_records_` 支持自动 flush 阈值。

### 1.2 问题

1. **重复打开/关闭文件句柄**：`model_handle.cc` 中的 `GetProfileOutput()` 在**每次 `Run()` 都执行 `fopen()` + `fclose()`**，在高频推理场景（如视频流 30fps）下产生不必要的系统调用开销。

2. **文件管理分散**：`ProfilingBackend` 的 `Flush()` 和 `model_handle.cc` 的 `GetProfileOutput()` 各自维护 `fopen`/`fclose` 逻辑，代码重复。

3. **CSV 表头重复判断**：`model_handle.cc` 通过 `csv_header` 局部变量 + 函数参数传递来判断是否写表头；`ProfilingBackend` 通过成员变量 `header_written_` 判断——两种机制各做各的。

4. **两个路径写同一个文件**：如果同时启用 backend profiling（`load,infer`）和 pipeline profiling（`infer` 阶段的 `input_pipeline`、`output_pipeline`），两个路径分别打开同一个 CSV 文件追加写入，存在写入交叉和缓存竞争风险。

---

## 二、方案设计

### 2.1 整体架构

引入一个统一的 `Profiler` 类（独立模块 `src/profiler/`），作为 profiling 的唯一入口，持有 `FILE*` 文件句柄，**构造时打开一次，析构时关闭**：

```
所有 Profiler 实例共享静态 FILE*（s_shared_fp_）
                              │
ModelManager::Init()
  │
  ├→ Profiler #1 (model="face")           ← 第一个：fopen + 写 CSV header
  │     └→ s_shared_fp_ = fopen(profile_NNN.csv, "w")
  │
  ├→ Profiler #2 (model="plate")          ← 后续：复用 s_shared_fp_
  │     └→ fp_ = s_shared_fp_
  │
  ├→ ProfilingBackend(inner, &profiler)   ← 持有 Profiler*，不再管理文件
  │     └→ profiler_->Push(...)
  │
  └→ ModelHandle::Run() 通过 entry_->profiler 访问
        └→ profiler_->Push(...)

最后一个 ~Profiler() → fclose + 重置静态状态
```

### 2.2 `Profiler` 类设计

`Profiler` 作为独立模块存放在 `src/profiler/` 下，同时持有常量、`ProfileRecord` 结构体和 `Profiler` 类。

关键设计：**所有 `Profiler` 实例通过静态成员共享同一个 `FILE*` 文件句柄**。第一个构造的 `Profiler` 负责创建文件并写入 CSV header，后续实例复用该句柄。最后一个 `Profiler` 析构时关闭文件并重置静态状态。

```cpp
// src/profiler/profiler.h
namespace atlas {
namespace backend {

// ── Phase / step 命名常量 ──────────────────────────────────
constexpr const char* kProfilePhaseLoad   = "load";
constexpr const char* kProfilePhaseInfer  = "infer";
constexpr const char* kProfilePhaseUnload = "unload";
// ... 步骤常量同上 ...

// ── ProfileRecord ──────────────────────────────────────────
struct ProfileRecord { /* ... */ };

// ── Profiler ───────────────────────────────────────────────
class Profiler {
 public:
    Profiler(const core::ProfileConfig& config, const std::string& model_id);
    ~Profiler();  // s_instance_count_--, 第0个析构时 fclose

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    bool ShouldProfile(const std::string& phase) const;
    void Push(const std::string& phase, const std::string& step,
              double duration_ms);
    static void FlushAll();

    static int64_t NowMs();
    static double  NowSteadyMs();

 private:
    static std::string DetermineOutputPath(const std::string& dir);

    void WriteLine(const std::string& model_id, const std::string& phase,
                   const std::string& step, double duration_ms,
                   int64_t timestamp_ms);

    // ── 所有实例共享的静态状态 ─────────────────────────
    static FILE*                       s_shared_fp_;
    static int                         s_instance_count_;
    static bool                        s_header_written_;
    static std::vector<ProfileRecord>  s_records_;
    static std::mutex                  s_mutex_;
    static int                         s_max_records_;

    const core::ProfileConfig*  config_;
    std::string                 model_id_;
    FILE*                       fp_;               // == s_shared_fp_
};

}  // namespace backend
}  // namespace atlas
```

### 2.3 `ProfilingBackend` 改造

`ProfilingBackend` 不再自己管理文件、缓冲区或表头。其头文件大幅精简，通过 `#include "src/profiler/profiler.h"` 引用 `Profiler`：

```cpp
// src/backend/base/profiling_backend.h
#include "src/profiler/profiler.h"

class ProfilingBackend : public IBackend {
 public:
    ProfilingBackend(std::unique_ptr<IBackend> inner, Profiler* profiler);
    // ...（其余接口不变）

 private:
    bool ShouldProfile(const std::string& phase) const;

    std::unique_ptr<IBackend> inner_;
    Profiler*                 profiler_;  // 非 owning，指向 ModelEntry::profiler_
};
```

- `Load()` / `Infer()` / `Unload()` 中：`if (ShouldProfile(...)) profiler_->Push(...)`
- 所有记录写入共享静态缓冲区 `s_records_`，达到 `max_records` 时自动 `FlushAll()`
- 最后一个 `Profiler` 析构时自动 `FlushAll()` + `fclose()`

### 2.4 `ModelEntry` 变更

```cpp
struct ModelEntry {
    ModelConfig                            config;
    std::unique_ptr<backend::IBackend>     backend;
    // ↑ 可能是 ProfilingBackend，持有 Profiler*
    std::unique_ptr<backend::Profiler>     profiler;   // NEW
    std::vector<pipeline::Pipeline>        input_pipelines;
    std::vector<pipeline::Pipeline>        output_pipelines;
    ProfileConfig                          profile;
    bool                                   loaded = false;
};
```

### 2.5 `ModelManager::Init()` 构造顺序

```cpp
// 先创建 Profiler（此时 backend 实例尚未创建）
auto profiler = std::make_unique<backend::Profiler>(manifest.profile, model.id);

// 创建原始 backend
auto backend = factory.Create(model.backend);

// 需要 profiling 时装饰
if (manifest.profile.enabled) {
    backend = std::make_unique<backend::ProfilingBackend>(
        std::move(backend), profiler.get());
}

// 移入 ModelEntry
ModelEntry entry;
entry.profiler = std::move(profiler);  // unique_ptr 移动后地址不变
entry.backend  = std::move(backend);   // ProfilingBackend 持有 profiler.get() 仍然有效
```

**地址稳定性保证**：`unique_ptr` 移动操作只转移所有权，不改变指向对象的地址，因此 `ProfilingBackend` 中持有的 `raw ptr` 在 move 后仍然有效。

### 2.6 `ModelHandle::Run()` 变更

删除 `model_handle.cc` 匿名空间中的三个函数：
- `ShouldProfilePipeline()` → `entry_->profiler->ShouldProfile(kProfilePhaseInfer)`
- `GetProfileOutput()` — 不再需要，文件句柄由 `Profiler` 统一管理
- `WritePipelineProfile()` → `entry_->profiler->Record()`

```cpp
// Run() 之前
bool profile_pipeline = entry_->profiler &&
                        entry_->profiler->ShouldProfile(backend::kProfilePhaseInfer);

// 记录 pipeline 耗时
if (profile_pipeline) {
    const double d = ProfileNowSteadyMs() - t_input_start;
    entry_->profiler->Record(backend::kProfilePhaseInfer,
                              backend::kProfileStepInputPipeline, d);
}
```

不再需要 `FILE* fp`、`csv_header` 标志和 `fclose()` 调用。时间戳由 `Profiler::Record()` 内部通过 `Profiler::NowMs()` 获取。

### 2.7 时间工具函数统一

两个工具函数在改造前各有副本：
- `profiling_backend.cc` 匿名空间：`NowMs()` / `NowSteadyMs()`
- `model_handle.cc` 匿名空间：`ProfileNowMs()` / `ProfileNowSteadyMs()`

统一方案：
- `NowMs()` / `NowSteadyMs()` 作为 `Profiler` 的 `public static` 方法，所有调用方统一使用 `backend::Profiler::NowSteadyMs()`
- `model_handle.cc` 中移除 `ProfileNowMs()`（仅 `WritePipelineProfile()` 内部使用），局部保留 `ProfileNowSteadyMs()` 别名用于 pipeline 阶段计时
- `profiling_backend.cc` 中移除匿名空间的 `NowMs()` / `NowSteadyMs()`，改用 `Profiler::NowSteadyMs()`

### 2.9 输出文件命名与计数器持久化

manifest 中的 `output_path` 配置字段语义从**文件路径**改为**目录路径**。目录下维护一个 `_counter` 文件持久化运行序号，`_counter.lock` 锁文件保证跨进程原子性：

```
/data/logs/
├── _counter           # 内容: "3\n" — 当前已运行次数
├── _counter.lock      # 临时锁文件（创建后立即删除）
├── profile_001.csv    # 第1次程序运行
├── profile_002.csv    # 第2次程序运行
└── profile_003.csv    # 第3次程序运行
```

首次 Profiler 构造时的流程：

1. 确保 `output_path` 目录存在（`mkdir`）
2. 原子地读 `_counter` → +1 → 写回（通过 `_counter.lock` 文件 + `fopen("wx")` 独占创建实现）
3. 用新的计数器值构造文件名：`profile_{counter:03d}.csv`

所有模型（所有 `Profiler` 实例）共享同一个文件，通过 `model_id` 字段区分：
```csv
model_id,phase,step,duration_ms,timestamp_ms
face_detection,infer,input_pipeline,0.123,...
face_detection,infer,forward,45.678,...
plate_recognition,infer,input_pipeline,0.234,...
```

| 职责 | 迁移前 | 迁移后 |
|------|--------|--------|
| 文件管理 | `ProfilingBackend` + `GetProfileOutput()` **两处** | `Profiler` 静态 `s_shared_fp_` **唯一一处**（所有实例共享） |
| 表头写入 | `ProfilingBackend` `header_written_` + 局部变量 `csv_header` | `Profiler` `header_written_` |
| CSV 写入 | `WriteCsv()` + `WritePipelineProfile()` | `Profiler::WriteLine()` |
| 管道阶段耗时记录 | `model_handle.cc` 即时写 | `Profiler::Push()` 共享缓冲区 |
| 后端耗时记录 | `ProfilingBackend` 缓冲后写 | `Profiler::Push()` 共享缓冲区 |
| 文件句柄生命周期 | 每个 `Run()` / `Flush()` 开关一次 | `Profiler` 构造到析构全程打开 |

---

## 三、改动清单

| 文件 | 改动 |
|------|------|
| `src/profiler/BUILD` | **新建** — profiler 模块构建规则 |
| `src/profiler/profiler.h` | **新建** — 常量 + `ProfileRecord` + `Profiler` 类声明；包含时间工具静态方法 `NowMs()` / `NowSteadyMs()` |
| `src/profiler/profiler.cc` | **新建** — `Profiler` 全部实现：文件打开/关闭、CSV 写入、header 管理、缓冲 flush |
| `src/backend/base/profiling_backend.h` | 精简：只保留 `ProfilingBackend` 类；包含 `src/profiler/profiler.h`；移除 `config_`、`model_id_`、`records_`、`header_written_` |
| `src/backend/base/profiling_backend.cc` | 移除 `Profiler` 实现；`ProfilingBackend` 简化，直接调用 `profiler_->BufferRecord()` |
| `src/backend/base/BUILD` | `profiling_backend` 依赖改为 `//src/profiler:profiler` |
| `src/core/model_manager.h` | `ModelEntry` 增加 `std::unique_ptr<backend::Profiler> profiler`；包含 `src/profiler/profiler.h` |
| `src/core/model_manager.cc` | `Init()` 中调整构造顺序：先 `Profiler`，再 `Backend`，最后 `ProfilingBackend` 包装 |
| `src/core/BUILD` | 增加 `//src/profiler:profiler` 依赖 |
| `src/api/model_handle.cc` | include 从 `profiling_backend.h` 改为 `profiler.h`；移除匿名空间三个函数，改用 `entry_->profiler` |
| `src/api/BUILD` | 增加 `//src/profiler:profiler` 依赖 |

---

## 四、验证方案

1. **编译验证**：项目完整编译通过，`all_tests` 全部通过

2. **行为等价性验证**：
   - 不开启 profiling（`enabled: false`）：`ModelEntry::profiler` 为 `nullptr`，`ModelHandle::Run()` 和 `ProfilingBackend` 均跳过 profiling 路径，**零额外文件打开开销**
   - 开启 pipeline profiling（`modules: "infer"`）：CSV 输出中包含 `input_pipeline`、`forward`、`output_pipeline` 三条记录
   - 开启 backend profiling（`modules: "load,infer,unload"`）：CSV 包含 `load/total`、`infer/forward`、`unload/total` 记录
   - 同时开启两者：所有记录写入同一个 CSV 文件，无冲突

3. **性能验证**：
   - 不开启 profiling 时：零性能损失（check 一次 `profiler == nullptr`）
   - 开启 profiling 时：所有 `fopen`/`fclose` 调用消除，只保留 1 次打开 + 1 次关闭

---

## 五、开放问题

1. **多线程安全**：当前 `Profiler` 是单线程设计。`ProfilingBackend` 可以被多个 `ModelHandle::Run()` 并发调用吗？当前设计下 `ProfilingBackend` 是 `ModelEntry` 的成员，而 `ModelHandle::Run()` 本身上层不保证线程安全，因此可以不加锁。未来若需要并发，需在 `Profiler` 内加锁保护 `fp_` 和 `records_`。

2. **文件所有权的清理时机**：`Profiler` 析构时 `fclose`，但 `ProfilingBackend` 的 `Unload()` 中调用了 `profiler_->Flush()`。如果 `Unload()` 后还有新的 BufferRecord 调用（理论上不应发生），它们不会被写入文件。需要在 `ProfilingBackend` 析构时再调一次 `Flush()` 确保数据落盘。

3. **多模型共享 Profiler**：当前设计每个模型有独立 `Profiler`，各写各的文件。如果多个模型配置不同的 `output_path`，这种行为是合理的。如果需要所有模型写入同一个文件（路径相同），也可以让多个模型共享同一个 `Profiler` 实例——但这属于后续优化，不在本次范围内。
