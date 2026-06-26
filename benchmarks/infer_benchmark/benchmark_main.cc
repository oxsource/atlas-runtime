#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "src/api/atlas_runtime.h"
#include "src/api/model_handle.h"
#include "src/backend/cpu/cpu_backend.h"
#include "src/backend/cpu/cpu_backend_context.h"
#include "src/utils/types.h"
#include "src/utils/version.h"

namespace {

constexpr int kDefaultWarmup     = 10;
constexpr int kDefaultIterations = 100;

// ---------------------------------------------------------------------------
// Creates a raw HWC uint8 image filled with a constant value.
// ---------------------------------------------------------------------------
atlas::utils::Tensor MakeRawImage(int h, int w, uint8_t fill) {
    atlas::utils::Tensor t;
    t.info.dtype  = atlas::utils::DataType::kUInt8;
    t.info.shape  = {h, w, 3};
    t.info.layout = "HWC";
    t.byte_size   = static_cast<size_t>(h * w * 3);
    t.data        = malloc(t.byte_size);
    t.owns_data   = true;
    std::memset(t.data, fill, t.byte_size);
    return t;
}

// ---------------------------------------------------------------------------
// Runs |handle| for |warmup| + |iterations| calls and returns the per-call
// latency vector (microseconds, excluding warmup).
// ---------------------------------------------------------------------------
std::vector<double> Measure(atlas::api::ModelHandle& handle,
                              const atlas::utils::Tensor& image,
                              int warmup, int iterations) {
    std::vector<atlas::utils::Tensor> outputs;

    for (int i = 0; i < warmup; ++i) {
        outputs.clear();
        handle.Run(image, &outputs);
    }

    std::vector<double> latencies;
    latencies.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        outputs.clear();
        auto t0 = std::chrono::high_resolution_clock::now();
        handle.Run(image, &outputs);
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return latencies;
}

// ---------------------------------------------------------------------------
// Prints P50/P95/P99/Mean latency (ms) and throughput (infer/sec).
// ---------------------------------------------------------------------------
void PrintStats(const std::string& label,
                std::vector<double>& v,
                int iterations) {
    std::sort(v.begin(), v.end());
    const double sum  = std::accumulate(v.begin(), v.end(), 0.0);
    const double mean = sum / static_cast<double>(v.size());
    auto pct = [&](double p) -> double {
        return v[static_cast<size_t>(p * static_cast<double>(v.size()))];
    };

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n[" << label << "]  " << iterations << " iterations\n";
    std::cout << "  P50        = " << pct(0.50) / 1000.0 << " ms\n";
    std::cout << "  P95        = " << pct(0.95) / 1000.0 << " ms\n";
    std::cout << "  P99        = " << pct(0.99) / 1000.0 << " ms\n";
    std::cout << "  Mean       = " << mean        / 1000.0 << " ms\n";
    std::cout << "  Throughput = " << 1e6 / mean          << " infer/sec\n";
}

// ---------------------------------------------------------------------------
// Parses a positive integer from argv[index], returns default on failure.
// ---------------------------------------------------------------------------
int ParseIntArg(int argc, char* argv[], int index, int default_val) {
    if (index >= argc) return default_val;
    try { return std::stoi(argv[index]); } catch (...) {}
    return default_val;
}

}  // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
//  Usage:
//    infer_benchmark <manifest_path> <model_id> [warmup] [iterations]
//
//  Environment:
//    BENCH_MODEL_DIR  – directory containing the benchmark .onnx file
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::cout << "Atlas Inference Benchmark  (v"
              << atlas::utils::VersionString() << ")\n";

    if (argc < 3) {
        std::cerr << "Usage: infer_benchmark <manifest_path> <model_id>"
                     " [warmup] [iterations]\n"
                  << "  Set BENCH_MODEL_DIR to the model directory.\n";
        return 1;
    }

    const std::string manifest_path = argv[1];
    const std::string model_id      = argv[2];
    const int         warmup        = ParseIntArg(argc, argv, 3, kDefaultWarmup);
    const int         iterations    = ParseIntArg(argc, argv, 4, kDefaultIterations);

    std::cout << "Manifest  : " << manifest_path << "\n"
              << "Model ID  : " << model_id      << "\n"
              << "Warmup    : " << warmup         << "\n"
              << "Iterations: " << iterations     << "\n";

    // Init runtime.
    atlas::api::AtlasRuntime runtime;
    auto ret = runtime.Init(manifest_path);
    if (ret != atlas::utils::ErrorCode::kOk) {
        std::cerr << "[ERROR] Init: "
                  << atlas::utils::ErrorCodeToString(ret) << "\n";
        return 1;
    }

    auto handle = runtime.GetModel(model_id);
    if (!handle.IsValid()) {
        std::cerr << "[ERROR] Model '" << model_id << "' not found.\n";
        return 1;
    }

    // Derive input spatial size from model metadata.
    const auto input_infos = handle.GetInputInfo();
    if (input_infos.empty() || input_infos[0].shape.size() < 4) {
        std::cerr << "[ERROR] Unexpected input shape.\n";
        return 1;
    }
    const int h = input_infos[0].shape[2];
    const int w = input_infos[0].shape[3];
    std::cout << "Input size: " << h << "×" << w << "\n";

    // Raw HWC uint8 image — Pipeline will preprocess automatically.
    auto image = MakeRawImage(h, w, 128);

    // Measure.
    auto latencies = Measure(handle, image, warmup, iterations);
    PrintStats(model_id, latencies, iterations);

    runtime.Release();
    return 0;
}
