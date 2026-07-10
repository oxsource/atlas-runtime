#include "src/backend/base/profiling_backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "src/core/manifest_config.h"
#include "src/utils/types.h"

#define LOG_TAG "Atlas::ProfilingBE"
#include "src/utils/logger.h"

namespace atlas {
namespace backend {

namespace {

// CSV column header written once at the start of output.
constexpr const char* kCsvHeader =
    "model_id,phase,step,duration_ms,timestamp_ms\n";

// Returns the current unix-epoch time in milliseconds.
int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Returns the current steady-clock time in milliseconds (for duration).
double NowSteadyMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Writes a single CSV record to |fp|.
void WriteCsv(FILE* fp, const ProfileRecord& r) {
    // model_id, phase, step may contain commas inside the original identifiers.
    // For simplicity we assume they do not (standard identifier naming).
    std::fprintf(fp, "%s,%s,%s,%.3f,%lld\n",
                 r.model_id.c_str(), r.phase.c_str(), r.step.c_str(),
                 r.duration_ms, static_cast<long long>(r.timestamp_ms));
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ProfilingBackend::ProfilingBackend(std::unique_ptr<IBackend> inner,
                                   core::ProfileConfig config,
                                   std::string model_id)
    : inner_(std::move(inner)),
      config_(std::move(config)),
      model_id_(std::move(model_id)) {
    ATLAS_LOGD("ProfilingBackend created for model: %s", model_id_.c_str());
}

ProfilingBackend::~ProfilingBackend() { Flush(); }

// ---------------------------------------------------------------------------
// IBackend interface
// ---------------------------------------------------------------------------

utils::ErrorCode ProfilingBackend::Load(const std::string& model_path,
                                         const core::ModelConfig& config,
                                         IBackendContext* ctx) {
    // Forward to inner backend first — profiling measures the real Load time.
    // We timestamp around the call rather than doing it step-by-step inside
    // the decorator, because the inner backend's implementation is opaque.

    const double t0 = NowSteadyMs();
    utils::ErrorCode ret = inner_->Load(model_path, config, ctx);
    const double elapsed = NowSteadyMs() - t0;

    if (ShouldProfile("load")) {
        Record("load", "total", elapsed, NowMs());
    }

    return ret;
}

utils::ErrorCode ProfilingBackend::Infer(
    const std::vector<utils::Tensor>& inputs,
    std::vector<utils::Tensor>& outputs) {
    const double t0 = NowSteadyMs();
    utils::ErrorCode ret = inner_->Infer(inputs, outputs);
    const double elapsed = NowSteadyMs() - t0;

    if (ShouldProfile("infer")) {
        Record("infer", "forward", elapsed, NowMs());
    }

    return ret;
}

void ProfilingBackend::Unload() {
    const double t0 = NowSteadyMs();
    inner_->Unload();
    const double elapsed = NowSteadyMs() - t0;

    if (ShouldProfile("unload")) {
        Record("unload", "total", elapsed, NowMs());
    }

    Flush();
}

std::vector<utils::TensorInfo> ProfilingBackend::GetInputInfo() const {
    return inner_->GetInputInfo();
}

std::vector<utils::TensorInfo> ProfilingBackend::GetOutputInfo() const {
    return inner_->GetOutputInfo();
}

utils::Span<void> ProfilingBackend::GetInputBuffer(size_t index) const {
    return inner_->GetInputBuffer(index);
}

utils::Span<void> ProfilingBackend::GetOutputBuffer(size_t index) const {
    return inner_->GetOutputBuffer(index);
}

utils::ErrorCode ProfilingBackend::SetInputBuffer(size_t index,
                                                   void* external_mem,
                                                   size_t byte_size) {
    return inner_->SetInputBuffer(index, external_mem, byte_size);
}

bool ProfilingBackend::IsLoaded() const { return inner_->IsLoaded(); }

std::string ProfilingBackend::Version() const { return inner_->Version(); }

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool ProfilingBackend::ShouldProfile(const std::string& phase) const {
    return config_.enabled &&
           core::ProfileModulesContain(config_.modules, phase);
}

void ProfilingBackend::Record(const std::string& phase,
                               const std::string& step,
                               double duration_ms,
                               int64_t timestamp_ms) {
    records_.push_back({model_id_, phase, step, duration_ms, timestamp_ms});
}

void ProfilingBackend::Flush() {
    if (records_.empty()) return;

    FILE* fp = stdout;
    bool should_close = false;
    if (!config_.output_path.empty()) {
        fp = std::fopen(config_.output_path.c_str(), "a");
        if (fp == nullptr) {
            ATLAS_LOGE("Failed to open profile output: %s",
                       config_.output_path.c_str());
            fp = stdout;
        } else {
            should_close = true;
        }
    }

    // Write CSV header only once across all Flush() calls.
    if (!header_written_) {
        std::fputs(kCsvHeader, fp);
        header_written_ = true;
    }

    for (const ProfileRecord& r : records_) {
        WriteCsv(fp, r);
    }
    std::fflush(fp);

    if (should_close) {
        std::fclose(fp);
    }

    records_.clear();
}

}  // namespace backend
}  // namespace atlas
