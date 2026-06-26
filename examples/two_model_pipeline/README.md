# Two-Model Pipeline Sample

Demonstrates dual-model inference using the Atlas public API.

## Prerequisites

- Bazel 6.5+
- Python 3 with `onnx` package (`pip install onnx`)

## Quick Start

```bash
# 1. Generate sample ONNX models
python3 examples/two_model_pipeline/gen_models.py /tmp/atlas_sample_models

# 2. Export the model directory
export SAMPLE_MODEL_DIR=/tmp/atlas_sample_models

# 3. Build
bazel build //examples/two_model_pipeline:two_model_pipeline

# 4. Run
./bazel-bin/examples/two_model_pipeline/two_model_pipeline \
    examples/two_model_pipeline/manifest.json
```

## Expected Output

```
Atlas Two-Model Pipeline Sample  (v1.0.0)

── Step 1: Initialize ──
Runtime initialized from: examples/two_model_pipeline/manifest.json

── Step 2: Get Model Handles ──
[detector]   input 'images'  shape: [1,3,32,32,]
[classifier] input 'images'  shape: [1,3,32,32,]

── Step 3: Create Input Image ──
Created 32×32 BGR image (HWC uint8).

── Step 4: Run Detector ──
Detector output: 3072 elements, top-1 index=<N>  value=<V>

── Step 5: Run Classifier ──
Classifier output: 3072 elements, top-1 index=<N>  value=<V>

── Result ──
detector  top-1: index=<N>  score=<V>
classifier top-1: index=<N>  score=<V>

Runtime released. Done.
```

## What This Sample Covers

| Atlas Module | Usage in Sample |
|---|---|
| `AtlasRuntime::Init()` | Parses manifest, creates shared `CpuBackendContext` |
| `AtlasRuntime::GetModel()` | Obtains `ModelHandle` (lazy load on first call) |
| `Pipeline::BuildInputPipeline()` | Auto-built from manifest: DtypeConvert → Resize → BGRToRGB → HWCToCHW → Normalize |
| `ModelHandle::Run()` | One call covers full preprocessing + inference |
| `ErrorCode` | All return values checked; no exceptions |
