// Copyright 2025 The Atlas Authors
// Atlas SNPE CPU Runtime Demonstration
// Shows how to use the SNPE backend with CPU as the inference runtime.
//
// Platform constraint: SNPE backend is only functional on linux_aarch64 /
// android_arm64. On other platforms the stub returns kBackendNotFound.
//
// This example demonstrates the symmetric zero-copy data path using
// GetInputTensor() and GetOutputTensor():
//   1. GetInputTensor(0) returns a writable Tensor backed by the SNPE
//      ITensor internal buffer — write input data directly, no malloc.
//   2. Run() detects the buffer is pre-filled and skips the input memcpy.
//   3. GetOutputTensor(0) returns a Tensor pointing to the SNPE output
//      memory — read results with no extra memcpy on the output side.
//
// Build:  bazel build //examples/snpe_cpu:snpe_cpu
// Run:    SAMPLE_MODEL_DIR=<path> ./bazel-bin/examples/snpe_cpu/snpe_cpu \
//             examples/snpe_cpu/manifest.json

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#define LOG_TAG "snpe_cpu"
#include "atlas/atlas.h"

namespace {

// ---------------------------------------------------------------------------
// Fills a float32 NCHW tensor with a synthetic gradient pattern.
// Each channel C at position (c, h, w) gets a distinct value so the output
// can be visually verified (ReLU is identity for positive values).
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
                    // Each pixel gets a distinct positive value (ReLU won't
                    // clamp anything).
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

}  // namespace

int main(int argc, char* argv[]) {
    atlas::utils::Logger::SetLevel(atlas::utils::Logger::Level::Debug);

    ATLAS_LOGD("Atlas SNPE CPU Runtime Sample  (v%s)",
               atlas::utils::VersionString());

    if (argc < 2) {
        ATLAS_LOGE("Usage: snpe_cpu <manifest_path>\n"
                   "  Set SAMPLE_MODEL_DIR to the directory containing\n"
                   "  relu_snpe.dlc before running.");
        return 1;
    }
    const std::string manifest_path = argv[1];

    // -- Step 1: Initialize AtlasRuntime ---------------------------------
    // Parses the manifest, creates a shared SnpeBackendContext,
    // and eagerly loads the SNPE model (load_strategy: eager).
    ATLAS_LOGD("-- Step 1: Initialize --");
    atlas::api::AtlasRuntime runtime;
    auto ret = runtime.Init(manifest_path);
    if (ret != atlas::utils::ErrorCode::kOk) {
        ATLAS_LOGE("[ERROR] Init failed: %s",
                   atlas::utils::ErrorCodeToString(ret));
        return 1;
    }
    ATLAS_LOGD("Runtime initialized from: %s", manifest_path.c_str());

    // -- Step 2: Obtain model handle -------------------------------------
    ATLAS_LOGD("-- Step 2: Get Model Handle --");
    auto model = runtime.GetModel("snpe_identity");
    if (!model.IsValid()) {
        ATLAS_LOGE("[ERROR] Failed to obtain model handle.");
        return 1;
    }

    // Print model metadata.
    const auto infos = model.GetInputInfo();
    {
        std::string shape_str = "[snpe_identity] input '" + infos[0].name
                                + "'  shape: [";
        for (int d : infos[0].shape) shape_str += std::to_string(d) + ",";
        shape_str += "]";
        ATLAS_LOGD("%s", shape_str.c_str());
    }

    ATLAS_LOGD("[snpe_identity] backend:      %s",
               model.GetBackend().c_str());
    ATLAS_LOGD("[snpe_identity] model_path:   %s",
               model.GetModelPath().c_str());
    ATLAS_LOGD("[snpe_identity] load_strategy: %d (%s)",
               model.GetLoadStrategy(),
               model.GetLoadStrategy() == 0 ? "eager" : "lazy");

    const auto cfg = model.GetConfig();
    if (!cfg.empty()) {
        std::string cfg_str = "[snpe_identity] config:       {";
        bool first = true;
        for (const auto& [k, v] : cfg) {
            if (!first) cfg_str += ", ";
            cfg_str += k + ": " + v;
            first = false;
        }
        cfg_str += "}";
        ATLAS_LOGD("%s", cfg_str.c_str());
    }

    // -- Step 3: Zero-copy input via GetInputTensor() --------------------
    // GetInputTensor() returns a Tensor whose data pointer points directly
    // into the SNPE ITensor internal buffer.  We write synthetic float32
    // NCHW data into this buffer — no malloc, no memcpy on the input path.
    ATLAS_LOGD("-- Step 3: Write Input via GetInputTensor() --");
    auto input_tensor = model.GetInputTensor(0);
    if (input_tensor.data == nullptr) {
        ATLAS_LOGE("[ERROR] GetInputTensor not supported by this backend.");
        return 1;
    }
    ATLAS_LOGD("Got input tensor: name='%s'  shape=[%s]  layout=%s  "
               "%zu bytes",
               input_tensor.info.name.c_str(),
               [&] {
                   std::string s;
                   for (int d : input_tensor.info.shape)
                       s += std::to_string(d) + ",";
                   return s;
               }()
                   .c_str(),
               input_tensor.info.layout.c_str(),
               input_tensor.byte_size);

    // Fill the ITensor buffer directly with synthetic float32 NCHW data.
    // The model expects shape [1, 3, 4, 4] (from the manifest).
    constexpr int kInN = 1, kInC = 3, kInH = 4, kInW = 4;
    if (static_cast<int>(input_tensor.byte_size / sizeof(float)) !=
        kInN * kInC * kInH * kInW) {
        ATLAS_LOGE("[ERROR] Unexpected input size: %zu",
                   input_tensor.byte_size);
        return 1;
    }
    FillGradientNCHW(static_cast<float*>(input_tensor.data),
                     kInN, kInC, kInH, kInW);
    ATLAS_LOGD("Filled %dx%dx%dx%d float32 NCHW tensor.", kInN, kInC, kInH,
               kInW);

    // -- Step 4: Run inference (zero-copy input + output) -----------------
    // The Tensor passed to Run() has the same data pointer as the ITensor
    // internal buffer.  SnpeBackend::Infer() detects this via pointer
    // comparison and skips the input memcpy.
    // Output tensors are zero-copy too: Infer() pushes Tensor with
    // owns_data=false pointing into the SNPE output_map.
    ATLAS_LOGD("-- Step 4: Run SNPE Inference (zero-copy input + output) --");
    std::vector<atlas::utils::Tensor> outputs;
    ret = model.Run(input_tensor, &outputs);

    if (ret == atlas::utils::ErrorCode::kBackendNotFound) {
        ATLAS_LOGD("SNPE backend not available on this platform "
                   "(stub returned kBackendNotFound).");
        ATLAS_LOGD("This is expected on non-aarch64 hosts. "
                   "Run on linux_aarch64 or android_arm64 to execute.");
        runtime.Release();
        ATLAS_LOGD("Runtime released. Done.");
        return 0;
    }

    if (ret != atlas::utils::ErrorCode::kOk) {
        ATLAS_LOGE("[ERROR] Inference failed: %s",
                   atlas::utils::ErrorCodeToString(ret));
        return 1;
    }

        // -- Step 5: Print results (zero-copy output, no extra memcpy) --------
    ATLAS_LOGD("-- Step 5: Results (zero-copy output) --");
    if (outputs.empty()) {
        ATLAS_LOGE("[ERROR] No output tensors returned.");
        return 1;
    }
    atlas::utils::Span<const float> out_data(
        static_cast<const float*>(outputs[0].data),
        outputs[0].byte_size / sizeof(float));
    int   top1  = ArgMaxAbs(out_data.data, out_data.size);
    float value = out_data[top1];
    ATLAS_LOGD("SNPE identity output: %zu elements, top-1 index=%d  value=%f",
               out_data.size, top1, value);

    // -- Step 6: Release --------------------------------------------------
    runtime.Release();
    ATLAS_LOGD("Runtime released. Done.");
    return 0;
}
