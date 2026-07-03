// Copyright 2025 The Atlas Authors
// Atlas SNPE CPU Runtime Demonstration
// Shows how to use the SNPE backend with CPU as the inference runtime.
//
// Platform constraint: SNPE backend is only functional on linux_aarch64 /
// android_arm64. On other platforms the stub returns kBackendNotFound.
//
// Build:  bazel build //examples/snpe_cpu:snpe_cpu
// Run:    SAMPLE_MODEL_DIR=<path> ./bazel-bin/examples/snpe_cpu/snpe_cpu \
//             examples/snpe_cpu/manifest.json

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "atlas/atlas.h"

#define LOG_TAG "snpe_cpu"
#include "src/utils/logger.h"

namespace {

// ---------------------------------------------------------------------------
// Creates a synthetic H×W BGR raw image (HWC uint8).
// Fills a gradient: B increases with row, G with column, R = 128.
// ---------------------------------------------------------------------------
atlas::utils::Tensor CreateSampleBGRImage(int h, int w) {
    atlas::utils::Tensor img;
    img.info.dtype  = atlas::utils::DataType::kUInt8;
    img.info.shape  = {h, w, 3};
    img.info.layout = "HWC";
    img.byte_size   = static_cast<size_t>(h * w * 3);
    img.data        = malloc(img.byte_size);
    img.owns_data   = true;

    uint8_t* p = static_cast<uint8_t*>(img.data);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            p[(r * w + c) * 3 + 0] = static_cast<uint8_t>(r * 255 / h);  // B
            p[(r * w + c) * 3 + 1] = static_cast<uint8_t>(c * 255 / w);  // G
            p[(r * w + c) * 3 + 2] = 128;                                  // R
        }
    }
    return img;
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
                   "  identity_snpe.dlc before running.");
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

    // -- Step 3: Prepare input -------------------------------------------
    // The Pipeline auto-converts: uint8 HWC BGR -> float32 NCHW RGB.
    ATLAS_LOGD("-- Step 3: Create Input Image --");
    constexpr int kH = 4, kW = 4;
    auto raw_image = CreateSampleBGRImage(kH, kW);
    ATLAS_LOGD("Created %dx%d BGR image (HWC uint8).", kH, kW);

    // -- Step 4: Run inference --------------------------------------------
    ATLAS_LOGD("-- Step 4: Run SNPE Inference (CPU Runtime) --");
    std::vector<atlas::utils::Tensor> outputs;
    ret = model.Run(raw_image, &outputs);

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

    // -- Step 5: Print results -------------------------------------------
    ATLAS_LOGD("-- Step 5: Results --");
    const float* out_data  = static_cast<const float*>(outputs[0].data);
    const size_t out_count = outputs[0].byte_size / sizeof(float);
    int   top1  = ArgMaxAbs(out_data, out_count);
    float value = out_data[top1];
    ATLAS_LOGD("SNPE identity output: %zu elements, top-1 index=%d  value=%f",
               out_count, top1, value);

    // -- Step 6: Release --------------------------------------------------
    runtime.Release();
    ATLAS_LOGD("Runtime released. Done.");
    return 0;
}
