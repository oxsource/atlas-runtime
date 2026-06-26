# Atlas AI Memory — API Contract

> Compiled from src/api/, src/utils/, src/core/, src/pipeline/, src/backend/base/.

## Public API (atlas::api)

### AtlasRuntime
```cpp
AtlasRuntime();
~AtlasRuntime();
ErrorCode Init(const std::string& manifest_path);  // Parse manifest, load eager models
ModelHandle GetModel(const std::string& model_id);   // Returns invalid handle if not found
void Release();                                       // Unload all, can re-Init after
bool IsInitialized() const;
```
- Uses PIMPL: `unique_ptr<ManifestParser>`, `unique_ptr<ModelManager>`
- Public header: `include/atlas/atlas_runtime.h` (no internal includes)
- Internal header: `src/api/atlas_runtime.h` (used by tests)

### ModelHandle
```cpp
ModelHandle() = default;
bool IsValid() const;
ErrorCode Run(const Tensor& raw_input, std::vector<Tensor>* outputs);           // Single input
ErrorCode Run(const std::vector<Tensor>& raw_inputs, std::vector<Tensor>* outputs); // Multi input
std::vector<TensorInfo> GetInputInfo() const;
std::vector<TensorInfo> GetOutputInfo() const;
```
- Non-owning; lifetime must not exceed AtlasRuntime
- NOT thread-safe (do not call Run() concurrently on same handle)
- Run() flow: input pipeline → batch dim insert → IBackend::Infer → output pipeline

## Utils Types (atlas::utils)

### DataType (enum class)
- `kUnknown=0, kFloat32, kFloat16, kInt8, kUInt8, kInt32`

### ErrorCode (enum class)
- `kOk=0, kInvalidArgument, kFileNotFound, kParseError, kVersionMismatch, kBackendNotFound, kInferFailed, kNotInitialized`

### TensorInfo
```cpp
struct TensorInfo {
    std::string name;
    std::vector<int> shape;        // -1 = dynamic
    DataType dtype = DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
};
```

### NormalizeParams
```cpp
struct NormalizeParams { std::vector<float> mean; std::vector<float> std; };
```

### Tensor (move-only)
```cpp
struct Tensor {
    TensorInfo info;
    void* data = nullptr;
    size_t byte_size = 0;
    bool owns_data = false;
    // Copy deleted, move enabled
    // Destructor frees if owns_data
};
```

### Helpers (inline)
- `const char* ErrorCodeToString(ErrorCode)` — "Ok", "InvalidArgument", etc.
- `size_t ElementByteSize(DataType)` — float32=4, float16=2, int8=1, uint8=1, int32=4
- `size_t ElementCount(const std::vector<int>& shape)` — treats -1 as 1
- `const char* VersionString()` — returns "1.0.0"
- `constexpr int kVersionMajor=1, kVersionMinor=0, kVersionPatch=0`

## Manifest Config (atlas::core)

### ManifestPipelineNode
```cpp
struct ManifestPipelineNode {
    std::string name;                                    // Unique within pipeline array
    std::unordered_map<std::string, std::string> params; // Flat key-value
};
```

### ManifestTensorInfo
```cpp
struct ManifestTensorInfo {
    std::string name;
    std::vector<int> shape;
    DataType dtype = DataType::kFloat32;
    std::string layout = "NCHW";
    bool has_normalize = false;
    NormalizeParams normalize;
    std::vector<ManifestPipelineNode> pipeline;  // Empty = auto-build
    TensorInfo ToTensorInfo() const;             // Drops pipeline field
};
```

### ModelConfig
```cpp
struct ModelConfig {
    std::string id, name, backend, model_path;
    LoadStrategy load_strategy = LoadStrategy::kEager;
    std::vector<ManifestTensorInfo> inputs, outputs;
    std::unordered_map<std::string, std::string> config;
};
```

### LoadStrategy (enum class)
- `kEager=0` — load at Init() time
- `kLazy` — load on first GetEntry()/Run()

## Backend Interface (atlas::backend)

### IBackend
```cpp
virtual ErrorCode Load(const std::string& model_path, const ModelConfig& config, IBackendContext* ctx=nullptr) = 0;
virtual ErrorCode Infer(const std::vector<Tensor>& inputs, std::vector<Tensor>& outputs) = 0;
virtual std::vector<TensorInfo> GetInputInfo() const = 0;
virtual std::vector<TensorInfo> GetOutputInfo() const = 0;
virtual void Unload() = 0;
virtual bool IsLoaded() const = 0;
```

### IBackendContext
```cpp
virtual std::string_view BackendType() const = 0;
virtual ~IBackendContext() = default;
```

### BackendFactory
- Singleton: `BackendFactory::Instance()`
- `Register(name, creator)` / `Create(name) → unique_ptr<IBackend>`
- `RegisterContext(name, creator)` / `CreateContext(name) → unique_ptr<IBackendContext>`
- `ListBackends() → vector<string>`

## Pipeline (atlas::pipeline)

### Pipeline
```cpp
void AddNode(unique_ptr<IPipelineNode>);
bool IsEmpty() const;
size_t NodeCount() const;
ErrorCode Run(const Tensor& input, Tensor* output) const;
static Pipeline BuildInputPipeline(const TensorInfo&);           // Auto: DtypeConvert→Resize→BGRToRGB→HWCToCHW→Normalize
static Pipeline BuildFromManifest(const vector<ManifestPipelineNode>&);  // User-declared
static Pipeline BuildOutputFromManifest(const vector<ManifestPipelineNode>&);
```

### IPipelineNode
```cpp
virtual ErrorCode Process(const Tensor& input, Tensor* output) = 0;
virtual string_view Name() const = 0;
virtual bool SupportsInPlace() const { return false; }
```

### PipelineNodeFactory
- Singleton: `PipelineNodeFactory::Instance()`
- `Register(name, creator)` / `Create(name, params) → unique_ptr<IPipelineNode>`
- `ListNodeNames() → vector<string>`

## Manifest JSON Format

```json
{
  "version": "1.0",
  "name": "app-name",
  "models": [{
    "id": "detector",
    "backend": "cpu",
    "model_path": "${MODEL_DIR}/detector.onnx",
    "load_strategy": "eager|lazy",
    "inputs": [{
      "name": "images",
      "shape": [1, 3, 224, 224],
      "dtype": "float32",
      "layout": "NCHW",
      "normalize": {"mean": [0.485], "std": [0.229]},
      "pipeline": [{"name": "resize", "params": {"height": 224, "width": 224}}]
    }],
    "outputs": [{"name": "out", "shape": [1,1000], "dtype": "float32"}],
    "config": {"num_threads": "2"}
  }]
}
```
- `${VAR}` env var expansion in model_path
- `pipeline` field optional (empty = auto-build)
- Pipeline node `name` must be unique within array
- `normalize` field used only when `pipeline` is absent (auto-build)
