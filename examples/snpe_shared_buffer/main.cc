// Copyright 2025 The Atlas Authors
//
// Atlas SNPE Shared Buffer Demonstration
//
// This example demonstrates two SNPE models sharing the same input memory
// buffer via the SnpeMemoryPool (key-based shared buffer).
//
// Usage:
//   1. Generate test DLC models:
//        python3 examples/snpe_shared_buffer/gen_models.py <output_dir>
//      This creates model_a.dlc and model_b.dlc (ReLU identity networks).
//
//   2. Build:
//        bazel build //examples/snpe_shared_buffer:snpe_shared_buffer
//
//   3. Run:
//        SAMPLE_MODEL_DIR=<output_dir> ./bazel-bin/examples/snpe_shared_buffer/snpe_shared_buffer \
//            examples/snpe_shared_buffer/manifest.json
//
// Flow:
//   - model_a and model_b both configure "shared_input": "camera_feed"
//   - When model_a loads, SnpeMemoryPool::AcquireShared("camera_feed", ...)
//     allocates aligned memory.  refcount=1.
//   - When model_b loads, AcquireShared("camera_feed", ...) returns the
//     *same* pointer.  refcount=2.
//   - Writing data to model_a's buffer is immediately visible via model_b.
//   - Both models execute their runtime; outputs should be identical since
//     both receive the same input.

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "snpe_shared_buffer"
#include "atlas/atlas.h"

namespace {

// ---------------------------------------------------------------------------
// Fills a float32 NCHW tensor with a synthetic gradient pattern.
// ---------------------------------------------------------------------------
void FillGradientNCHW(float* data, int n, int c, int h, int w) {
    for (int cn = 0; cn < n; ++cn) {
        for (int cc = 0; cc < c; ++cc) {
            for (int ch = 0; ch < h; ++ch) {
                for (int cw = 0; cw < w; ++cw) {
                    const size_t idx =
                        static_cast<size_t>(cn) * c * h * w +
                        static_cast<size_t>(cc) * h * w +
                        static_cast<size_t>(ch) * w +
                        static_cast<size_t>(cw);
                    data[idx] = static_cast<float>(idx + 1);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Returns the index of the element with the largest absolute value.
// ---------------------------------------------------------------------------
int ArgMaxAbs(const float* data, size_t count) {
    int   best   = 0;
    float best_v = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float v = data[i] >= 0.0f ? data[i] : -data[i];
        if (v > best_v) { best_v = v; best = static_cast<int>(i); }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Compares two float buffers for exact equality.
// ---------------------------------------------------------------------------
bool BuffersEqual(const float* a, const float* b, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (std::fabs(a[i] - b[i]) > 1e-6f) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    atlas::utils::Logger::SetLevel(atlas::utils::Logger::Level::Debug);

    ATLAS_LOGD("Atlas SNPE Shared Buffer Sample  (v%s)",
               atlas::utils::VersionString());

    if (argc < 2) {
        ATLAS_LOGE("Usage: snpe_shared_buffer <manifest_path>\n"
                   "  Set SAMPLE_MODEL_DIR to the directory containing\n"
                   "  model_a.dlc and model_b.dlc before running.");
        return 1;
    }
    const std::string manifest_path = argv[1];

    // =====================================================================
    // Step 1: Initialize AtlasRuntime (shared context)
    // =====================================================================
    // The runtime parses the manifest and creates one SnpeBackendContext
    // shared by both models.  The context owns the SnpeMemoryPool.
    ATLAS_LOGD("-- Step 1: Initialize Runtime --");
    atlas::api::AtlasRuntime runtime;
    auto ret = runtime.Init(manifest_path);
    if (ret != atlas::utils::ErrorCode::kOk) {
        ATLAS_LOGE("[ERROR] Init failed: %s",
                   atlas::utils::ErrorCodeToString(ret));
        return 1;
    }
    ATLAS_LOGD("Runtime initialized from: %s", manifest_path.c_str());

    // =====================================================================
    // Step 2: Obtain model handles for both models
    // =====================================================================
    ATLAS_LOGD("-- Step 2: Get Model Handles --");
    auto model_a = runtime.GetModel("model_a");
    if (!model_a.IsValid()) {
        ATLAS_LOGE("[ERROR] Failed to obtain model_a handle.");
        return 1;
    }
    auto model_b = runtime.GetModel("model_b");
    if (!model_b.IsValid()) {
        ATLAS_LOGE("[ERROR] Failed to obtain model_b handle.");
        return 1;
    }

    // Print model metadata.
    {
        auto infos = model_a.GetInputInfo();
        if (!infos.empty()) {
            std::string s = "[model_a] input '" + infos[0].name + "'  shape: [";
            for (int d : infos[0].shape) s += std::to_string(d) + ",";
            s += "]  dtype=" + std::to_string(static_cast<int>(infos[0].dtype));
            ATLAS_LOGD("%s", s.c_str());
        }
    }
    {
        auto infos = model_b.GetInputInfo();
        if (!infos.empty()) {
            std::string s = "[model_b] input '" + infos[0].name + "'  shape: [";
            for (int d : infos[0].shape) s += std::to_string(d) + ",";
            s += "]  dtype=" + std::to_string(static_cast<int>(infos[0].dtype));
            ATLAS_LOGD("%s", s.c_str());
        }
    }

    ATLAS_LOGD("[model_a] backend:    %s", model_a.GetBackend().c_str());
    ATLAS_LOGD("[model_a] model_path: %s", model_a.GetModelPath().c_str());
    ATLAS_LOGD("[model_b] backend:    %s", model_b.GetBackend().c_str());
    ATLAS_LOGD("[model_b] model_path: %s", model_b.GetModelPath().c_str());

    // =====================================================================
    // Step 3: Write input data via model_a's input buffer
    // =====================================================================
    // Both models share the same underlying memory via shared_input config.
    // Writing to model_a's buffer is equivalent to writing to model_b's.
    ATLAS_LOGD("-- Step 3: Write Input via model_a --");
    auto input_a = model_a.GetInputTensor(0);
    if (input_a.data == nullptr) {
        ATLAS_LOGE("[ERROR] GetInputTensor not supported by this backend.");
        return 1;
    }

    constexpr int kInN = 1, kInC = 3, kInH = 4, kInW = 4;
    FillGradientNCHW(static_cast<float*>(input_a.data),
                     kInN, kInC, kInH, kInW);
    ATLAS_LOGD("Wrote %dx%dx%dx%d gradient to model_a buffer.", kInN, kInC, kInH, kInW);

    // =====================================================================
    // Step 4: Verify that model_b sees the same data (shared memory)
    // =====================================================================
    ATLAS_LOGD("-- Step 4: Verify Shared Memory --");
    auto input_b = model_b.GetInputTensor(0);
    if (input_b.data == nullptr) {
        ATLAS_LOGE("[ERROR] GetInputTensor not supported by this backend.");
        return 1;
    }

    // Both tensors should point to the same memory location.
    ATLAS_LOGD("model_a buffer: %p", input_a.data);
    ATLAS_LOGD("model_b buffer: %p", input_b.data);

    if (input_a.data == input_b.data) {
        ATLAS_LOGD("✓ Shared buffer verified: both models use identical memory.");
    } else {
        ATLAS_LOGW("Buffers differ — models may not share memory on this platform.");
    }

    // =====================================================================
    // Step 5: Run inference on both models
    // =====================================================================
    ATLAS_LOGD("-- Step 5: Run Inference --");

    // Run model_a.
    std::vector<atlas::utils::Tensor> outputs_a;
    ret = model_a.Run(input_a, &outputs_a);
    if (ret == atlas::utils::ErrorCode::kBackendNotFound) {
        ATLAS_LOGD("SNPE backend not available on this platform (stub).");
        runtime.Release();
        return 0;
    }
    if (ret != atlas::utils::ErrorCode::kOk) {
        ATLAS_LOGE("[ERROR] model_a inference failed: %s",
                   atlas::utils::ErrorCodeToString(ret));
        return 1;
    }
    ATLAS_LOGD("model_a inference OK, %zu output(s).", outputs_a.size());

    // Run model_b using the same shared input.
    std::vector<atlas::utils::Tensor> outputs_b;
    ret = model_b.Run(input_b, &outputs_b);
    if (ret != atlas::utils::ErrorCode::kOk) {
        ATLAS_LOGE("[ERROR] model_b inference failed: %s",
                   atlas::utils::ErrorCodeToString(ret));
        return 1;
    }
    ATLAS_LOGD("model_b inference OK, %zu output(s).", outputs_b.size());

    // =====================================================================
    // Step 6: Compare outputs
    // =====================================================================
    ATLAS_LOGD("-- Step 6: Compare Outputs --");

    for (size_t i = 0; i < outputs_a.size(); ++i) {
        const float* data_a = static_cast<const float*>(outputs_a[i].data);
        const size_t count_a = outputs_a[i].byte_size / sizeof(float);

        if (i < outputs_b.size()) {
            const float* data_b = static_cast<const float*>(outputs_b[i].data);
            const size_t count_b = outputs_b[i].byte_size / sizeof(float);

            ATLAS_LOGD("Output[%zu]: model_a %zu floats, model_b %zu floats",
                       i, count_a, count_b);

            if (count_a == count_b && BuffersEqual(data_a, data_b, count_a)) {
                ATLAS_LOGD("  ✓ Outputs match (shared input → identical results).");
            } else {
                ATLAS_LOGD("  ⚠ Outputs differ — models may produce different results.");
            }

            int top_a = ArgMaxAbs(data_a, count_a);
            int top_b = ArgMaxAbs(data_b, count_b);
            ATLAS_LOGD("  model_a top-1[%d]=%f  model_b top-1[%d]=%f",
                       top_a, data_a[top_a], top_b, data_b[top_b]);
        }
    }

    // =====================================================================
    // Step 7: Release
    // =====================================================================
    ATLAS_LOGD("-- Step 7: Release --");
    runtime.Release();
    ATLAS_LOGD("Runtime released. Done.");
    return 0;
}
