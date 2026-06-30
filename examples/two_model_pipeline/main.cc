#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "atlas/atlas.h"

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
// Stands in for "top-1 class index" when using identity test models.
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
// Prints a formatted section banner.
// ---------------------------------------------------------------------------
void Banner(const char* title) {
    std::cout << "\n── " << title << " ──\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::cout << "Atlas Two-Model Pipeline Sample  (v"
              << atlas::utils::VersionString() << ")\n";

    if (argc < 2) {
        std::cerr << "Usage: two_model_pipeline <manifest_path>\n"
                  << "  Set SAMPLE_MODEL_DIR to the directory containing\n"
                  << "  detector.onnx and classifier.onnx before running.\n";
        return 1;
    }
    const std::string manifest_path = argv[1];

    // ── Step 1: Initialize AtlasRuntime ─────────────────────────────────
    // Parses the manifest, creates one shared CpuBackendContext (Ort::Env),
    // and eagerly loads "detector". "classifier" is lazy.
    Banner("Step 1: Initialize");
    atlas::api::AtlasRuntime runtime;
    auto ret = runtime.Init(manifest_path);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Init failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }
    std::cout << "Runtime initialized from: " << manifest_path << "\n";

    // ── Step 2: Obtain model handles ────────────────────────────────────
    // GetModel("classifier") triggers lazy loading here.
    Banner("Step 2: Get Model Handles");
    auto detector   = runtime.GetModel("detector");
    auto classifier = runtime.GetModel("classifier");

    if (!detector.IsValid() || !classifier.IsValid()) {
        std::cerr << "[ERROR] Failed to obtain model handles.\n";
        return 1;
    }

    // Print I/O metadata and configuration.
    for (const auto& [name, handle] : std::vector<std::pair<std::string,
                                                  atlas::api::ModelHandle*>>{
             {"detector",   &detector},
             {"classifier", &classifier}}) {
        const auto infos = handle->GetInputInfo();
        std::cout << "[" << name << "] input '"
                  << infos[0].name << "'  shape: [";
        for (int d : infos[0].shape) std::cout << d << ",";
        std::cout << "]\n";

        // Config fields exposed by Proposal-006.
        const int ls = handle->GetLoadStrategy();
        std::cout << "[" << name << "] backend:      " << handle->GetBackend()    << "\n"
                  << "[" << name << "] model_path:   " << handle->GetModelPath()  << "\n"
                  << "[" << name << "] load_strategy: " << ls
                  << " (" << (ls == 0 ? "eager" : "lazy") << ")\n";

        const auto cfg = handle->GetConfig();
        if (!cfg.empty()) {
            std::cout << "[" << name << "] config:       {";
            bool first = true;
            for (const auto& [k, v] : cfg) {
                if (!first) std::cout << ", ";
                std::cout << k << ": " << v;
                first = false;
            }
            std::cout << "}\n";
        }
    }

    // ── Step 3: Prepare raw input image ─────────────────────────────────
    Banner("Step 3: Create Input Image");
    constexpr int kH = 32, kW = 32;
    auto raw_image = CreateSampleBGRImage(kH, kW);
    std::cout << "Created " << kH << "×" << kW
              << " BGR image (HWC uint8).\n";

    // ── Step 4: Run detector ─────────────────────────────────────────────
    // The Pipeline automatically performs:
    //   DtypeConvert (uint8 → float32, cast only)
    //   Resize       (32×32 → 32×32, no-op)
    //   BGRToRGB     (channel swap)
    //   HWCToCHW     ([H,W,C] → [C,H,W])
    //   Normalize    ((x/255 − mean) / std,  per manifest config)
    // Then CpuBackend::Infer() is called.
    Banner("Step 4: Run Detector");
    std::vector<atlas::utils::Tensor> det_outputs;
    ret = detector.Run(raw_image, &det_outputs);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Detector failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }

    const float* det_data  = static_cast<const float*>(det_outputs[0].data);
    const size_t det_count = det_outputs[0].byte_size / sizeof(float);
    int   det_top1  = ArgMaxAbs(det_data, det_count);
    float det_value = det_data[det_top1];
    std::cout << "Detector output: " << det_count
              << " elements, top-1 index=" << det_top1
              << "  value=" << det_value << "\n";

    // ── Step 5: Run classifier (same raw image) ──────────────────────────
    // No normalize in manifest → Pipeline skips NormalizeNode.
    // Both models share the same CpuBackendContext (single Ort::Env).
    Banner("Step 5: Run Classifier");
    std::vector<atlas::utils::Tensor> cls_outputs;
    ret = classifier.Run(raw_image, &cls_outputs);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Classifier failed: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }

    const float* cls_data  = static_cast<const float*>(cls_outputs[0].data);
    const size_t cls_count = cls_outputs[0].byte_size / sizeof(float);
    int   cls_top1  = ArgMaxAbs(cls_data, cls_count);
    float cls_value = cls_data[cls_top1];
    std::cout << "Classifier output: " << cls_count
              << " elements, top-1 index=" << cls_top1
              << "  value=" << cls_value << "\n";

    // ── Step 6: Combined result ──────────────────────────────────────────
    Banner("Result");
    std::cout << "detector  top-1: index=" << det_top1
              << "  score=" << det_value  << "\n";
    std::cout << "classifier top-1: index=" << cls_top1
              << "  score=" << cls_value  << "\n";

    // ── Step 7: Release ──────────────────────────────────────────────────
    runtime.Release();
    std::cout << "\nRuntime released. Done.\n";
    return 0;
}
