# SNPE CPU Shared Library Comparison Test

Demonstrates **controlled-variable comparison** between the CPU backend (ONNX model via ONNX Runtime) and the SNPE backend (DLC model via SNPE CPU Runtime) using the Atlas shared library (Makefile, no Bazel).

## Scope

- **Goal**: Verify that CPU (ONNX Runtime) and SNPE (DLC) produce consistent inference results under identical input, while simultaneously validating SNPE backend integration via the shared library.
- **Method**: Use a plain `Makefile` to build and test the comparison.
- **Integration Point**: Link against the Atlas shared library (`libatlas.so` / `libatlas.dylib`) via `ATLAS_SDK`.
- **Model**: ReLU operator — functionally equivalent to Identity for all-non-negative inputs, while being compatible with SNPE v1 converter (which does not support Identity).

## Structure

```
examples/snpe_cpu_shared/
├── Makefile              # Build system (shared-library style)
├── main.cc               # Comparison test program
├── manifest.json         # Dual-model manifest (CPU ONNX + SNPE DLC)
├── relu.onnx             # ONNX ReLU model for CPU backend (generated)
├── gen_models.py         # Script to generate ONNX + DLC model pair
└── README.md             # This file
```

## Prerequisites

- Atlas SDK built via `tools/build_release.sh`
- For DLC generation: SNPE SDK installed (`SNPE_SDK_PATH` environment variable)
- Python 3 with `onnx` package (`pip install onnx`)
- **Android workflow**: Android NDK (for cross-compilation) + adb-connected device
- Target: Android arm64 for SNPE backend; Linux aarch64 or x86_64 for CPU-only comparison

## Quick Start (Host — CPU only)

### 1. Generate models

```bash
# Generate ONNX model (always required)
python3 examples/snpe_cpu_shared/gen_models.py

# To also generate the DLC model, set SNPE_SDK_PATH first:
export SNPE_SDK_PATH=/path/to/snpe-sdk
python3 examples/snpe_cpu_shared/gen_models.py /tmp/snpe_sample_models
```

### 2. Build the SDK

```bash
# From workspace root
./tools/build_release.sh
```

### 3. Build and test

```bash
cd examples/snpe_cpu_shared

# Export paths
export ATLAS_SDK=../../atlas-sdk/linux-x86_64   # or linux-aarch64
export SAMPLE_MODEL_DIR=/tmp/snpe_sample_models   # DLC model directory

# Build
make

# Test
make test
```

## Android Workflow (SNPE)

The Android workflow uses `tools/snpe_cpu_shared.sh` to automate the full pipeline:
1. Activate `conda snpe36` environment and generate DLC + ONNX models
2. Cross-compile Android arm64 binary via Makefile with `CROSS_COMPILE`
3. Push resources to the Android device via adb
4. Run the comparison test on device

### Prerequisites (Android)

- Android NDK installed with `aarch64-linux-android24-` toolchain in PATH
- Atlas SDK built for Android: `./tools/build_release.sh --platform android_arm64`
- SNPE SDK installed (available via conda environment or set `SNPE_SDK_PATH`)
- Conda environment `snpe36` with SNPE SDK and Python packages
- Android device connected via adb

### Step-by-step Android Workflow

```bash
# Step 1: Activate conda snpe36 + set environment variables
source tools/snpe_cpu_shared.sh env

# Step 2: Generate models (ONNX + DLC)
bash tools/snpe_cpu_shared.sh gen

# Step 3: Cross-compile Android arm64 binary
bash tools/snpe_cpu_shared.sh build

# Step 4: Push resources to Android device
bash tools/snpe_cpu_shared.sh push

# Step 5: Run on device
bash tools/snpe_cpu_shared.sh run
```

### All-in-one Android workflow

```bash
# Source env first, then run the full pipeline
source tools/snpe_cpu_shared.sh env
bash tools/snpe_cpu_shared.sh all
```

## Expected Output (on supported platform with both backends)

```
Atlas SNPE/CPU Comparison Test (Shared Library)

[OK]   Init returns kOk
[OK]   IsInitialized == true
[OK]   GetModel('relu_cpu') is valid
[OK]   CPU model backend == 'cpu'
[OK]   GetModel('relu_snpe') is valid
[OK]   SNPE model backend == 'snpe'
[OK]   CPU Run returns kOk
[OK]   CPU Run produces 1 output tensor
[OK]   SNPE Run returns kOk
[OK]   SNPE Run produces 1 output tensor
[OK]   [Compare] CPU Run returns kOk
[OK]   [Compare] SNPE Run returns kOk
[OK]   [Compare] Output shapes match
[OK]   [Compare] Output values match within tolerance
        max_diff = <small_number> (tolerance = 1e-05)
[OK]   GetModel('nonexistent') is invalid
[OK]   After Release, IsInitialized == false
[OK]   Re-init succeeds
[OK]   Post-reinit IsInitialized == true

0 failure(s)
```

## Expected Output (on non-aarch64 host — SNPE stub)

```
Atlas SNPE/CPU Comparison Test (Shared Library)

[OK]   Init returns kOk
[OK]   IsInitialized == true
[OK]   GetModel('relu_cpu') is valid
[OK]   CPU model backend == 'cpu'
[OK]   GetModel('relu_snpe') is valid
[OK]   SNPE model backend == 'snpe'
[OK]   CPU Run returns kOk
[OK]   CPU Run produces 1 output tensor
[OK]   SNPE backend not available on this platform (kBackendNotFound). Skipping comparison tests.
[OK]   GetModel('nonexistent') is invalid
[OK]   After Release, IsInitialized == false
[OK]   Re-init succeeds
[OK]   Post-reinit IsInitialized == true

0 failure(s)
```

## What This Sample Covers

| Atlas Module | Usage in Sample |
|---|---|
| `AtlasRuntime::Init()` | Parses dual-model manifest |
| `AtlasRuntime::GetModel()` | Obtains handles for both CPU and SNPE models |
| `ModelHandle::Run()` | Single-call preprocessing + inference on each backend |
| `ErrorCode::kBackendNotFound` | Graceful handling on unsupported platforms |
| **Controlled-variable comparison** | Same input → two backends → element-wise output comparison |

## Test Cases

| # | Test | Expected |
|---|------|----------|
| 1 | Runtime Init | `kOk` |
| 2 | `GetModel("relu_cpu")` | `IsValid == true`, backend `"cpu"` |
| 3 | `GetModel("relu_snpe")` | `IsValid == true`, backend `"snpe"` |
| 4 | CPU Run | `kOk`, 1 output |
| 5 | SNPE Run | `kOk` (or `kBackendNotFound` on non-aarch64) |
| 6 | Output shape comparison | Both shapes match |
| 7 | Output value comparison | `max_diff <= 1e-5` |
| 8 | `GetModel("nonexistent")` | `IsValid == false` |
| 9 | Release → re-init | Both succeed |

## Environment Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `ATLAS_SDK` | Path to Atlas SDK root directory | Required |
| `SAMPLE_MODEL_DIR` | Directory containing `relu_snpe.dlc` | Required for SNPE |
| `VERBOSE` | Set to `1` for verbose build output | `0` |

## Related Documentation

- [Proposal-011: SNPE CPU Shared Library Comparison Test](../../docs/proposals/011-snpe-cpu-shared-library-comparison-test.md)
- [SNPE Backend Proposal](../../docs/proposals/002-snpe-backend.md)
- [Shared Library Sample](../shared_library/) — Non-Bazel integration with CPU backend only
- [SNPE CPU Bazel Sample](../snpe_cpu/) — Bazel-based SNPE example