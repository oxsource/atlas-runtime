# External Consumer Integration Test

This directory demonstrates how **non-Bazel** external projects can integrate with and use the Atlas SDK.

## Scope

- **Goal**: Verify that external projects can compile, link, and run against the Atlas shared library without Bazel.
- **Method**: Use a plain `Makefile` to build and test the integration.
- **Integration Point**: Use both direct `-I/-L` flags and `pkg-config` detection.
- **Test Coverage**: Full API surface (manifest loading, model handle, tensor inference, output validation).

## Structure

```
tests/external_consumer/
├── Makefile              # Build system for external project
├── main.cc               # Integration test code
├── manifest.json         # Test manifest (references identity model)
└── README.md             # This file
```

## Quick Start

### Option 1: Build and Test with Bazel-generated SDK

```bash
# From workspace root, build the SDK for current platform
./tools/build_release.sh

# Detect the output directory (e.g., atlas-sdk/linux-x86_64)
ls atlas-sdk/

# Build the external consumer test
cd tests/external_consumer
ATLAS_SDK=../../atlas-sdk/linux-x86_64 make

# Run the test
ATLAS_SDK=../../atlas-sdk/linux-x86_64 make test
```

Expected output:
```
=== Atlas External Consumer Integration Test ===
...
[7] Verifying output...
  ✓ Output values match input (identity model verified)

==================================================
✓ All integration tests PASSED!
...
```

### Option 2: Build and Test with System-Installed Atlas

If Atlas is installed to system prefix (e.g., via `tools/install_atlas.sh /usr/local`):

```bash
cd tests/external_consumer

# Build (auto-detects via pkg-config)
make

# Run test
make test
```

### Option 3: Cross-Platform Build

```bash
# Build SDK for target platform
./tools/build_release.sh --platform linux_aarch64 --prefix ./atlas-sdk

# Test with that SDK
cd tests/external_consumer
ATLAS_SDK=../../atlas-sdk/linux-aarch64 make
ATLAS_SDK=../../atlas-sdk/linux-aarch64 make test
```

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` or `make all` | Compile the test executable |
| `make test` | Build and run the integration test |
| `make clean` | Remove build artifacts |
| `make help` | Display help information |

## Environment Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `ATLAS_SDK` | Path to Atlas SDK root directory | Auto-detect via pkg-config |
| `BUILD_DIR` | Build output directory | `build/` |
| `VERBOSE` | Set to `1` for verbose build output | `0` |

## Test Details

The integration test (`main.cc`) verifies:

1. **AtlasRuntime initialization** from manifest JSON
2. **Model enumeration** and metadata queries
3. **ModelHandle retrieval** for specific models
4. **Input tensor preparation** with test data
5. **Inference execution** on CPU backend
6. **Output validation** (identity model verification)
7. **Error handling** for missing files and API failures

## Model Used

- **File**: `../../backend/cpu/test_data/identity_1x3x4x4.onnx`
- **Type**: Identity operator (output = input)
- **Shape**: `[1, 3, 4, 4]` (batch=1, channels=3, height=4, width=4)
- **Data Type**: Float32
- **Purpose**: Simplest possible model to verify end-to-end pipeline

## Platform Support

| Platform | Supported | Notes |
|----------|-----------|-------|
| macOS (Apple Silicon) | ✓ | `.dylib` linking, RPATH via `-Wl,-rpath` |
| macOS (Intel) | ✓ | Same as Apple Silicon |
| Linux (x86_64) | ✓ | `.so` linking, RPATH via `-Wl,-rpath` |
| Linux (aarch64) | ✓ | Requires ARM64 ONNX Runtime |
| Android | ✗ | Android NDK Makefile setup beyond scope |
| Windows | ✗ | Not supported in this release |

## Troubleshooting

### Build Error: "fatal error: atlas/atlas_runtime.h: No such file or directory"

**Cause**: `ATLAS_SDK` is not set or SDK headers were not copied.

**Solution**:
```bash
# Ensure SDK was built successfully
./tools/build_release.sh
ls -la atlas-sdk/*/include/atlas/

# Explicitly set ATLAS_SDK
ATLAS_SDK=./atlas-sdk/linux-x86_64 make
```

### Runtime Error: "libatlas.so: cannot open shared object file"

**Cause**: Library path is not set. Makefile's RPATH may be overridden.

**Solution**:
```bash
# Set library paths explicitly
LD_LIBRARY_PATH=./atlas-sdk/linux-x86_64/lib ./build/test_external_consumer manifest.json ...
```

### Runtime Error: "Cannot open manifest file" or "Cannot open model file"

**Cause**: Relative paths are incorrect or working directory is wrong.

**Solution**:
```bash
# Ensure you run from tests/external_consumer/
cd tests/external_consumer

# Or use absolute paths
ATLAS_SDK=/full/path/to/atlas-sdk/linux-x86_64 make test
```

### pkg-config Not Found

**Cause**: pkg-config is not installed or Atlas SDK is not in pkg-config search path.

**Solution**:
```bash
# Install pkg-config
# - macOS: brew install pkg-config
# - Ubuntu: sudo apt install pkg-config

# Or set ATLAS_SDK explicitly instead of relying on pkg-config
ATLAS_SDK=./atlas-sdk/linux-x86_64 make
```

## Verification Checklist

After running `make test`, verify:

- [ ] "✓ All integration tests PASSED!" message appears
- [ ] No "ERROR:" or "FAILED" messages
- [ ] Output tensor values match input (identity verification)
- [ ] Exit code is 0 (`echo $?` after running test)

## Use Cases

This external consumer test validates:

1. **SDK Portability**: SDK can be moved to other machines/CI environments
2. **Non-Bazel Integration**: Third-party projects using CMake, Make, autotools, etc.
3. **pkg-config Compatibility**: Standard integration mechanism for libraries
4. **Cross-Platform Builds**: Same test runs on macOS and Linux with platform-specific SDK

## Related Documentation

- [`docs/phase6.md`](../../docs/phase6.md) - Public API and distribution
- [`tools/build_release.sh`](../../tools/build_release.sh) - SDK build script
- [`examples/two_model_pipeline/`](../../examples/two_model_pipeline/) - Bazel-based example
- [`tests/public/`](../public/) - Bazel-based public API tests

## Future Enhancements

- [ ] CMake integration test (complement to Makefile)
- [ ] Conan package integration
- [ ] Platform-specific binary distribution and GH Releases
- [ ] CI integration (GitHub Actions for automated SDK builds and tests)
